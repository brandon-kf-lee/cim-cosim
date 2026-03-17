// mem_controller.cpp

#include "include/mem_controller.h"

using namespace sc_core;
using namespace tlm;

/**
 * Memory Controller constructor
 *
 * Creates the TLM target socket for CPU communication, initiator socket for SRAM access,
 * and allocates DMA buffer
 *
 * @param name SystemC module name for this instance
 *
 */
Mem_Controller::Mem_Controller(sc_module_name name): 
    sc_module(name), 
    t_cpu_socket("t_cpu_socket"), 
    i_sram_socket("i_sram_socket"), 
    irq("irq")
{
    t_cpu_socket.register_b_transport(this, &Mem_Controller::b_transport);

    // Initialize interrupt
    irq.initialize(false);
    
    // Start irq manager
    // TODO: is this the proper way to start a SystemC thread?
    SC_THREAD(irq_manager);
}

/**
 * 
 * Memory Controller destructor
 * 
 * Clean up dynamically allocated memory controller resources
 */
Mem_Controller::~Mem_Controller(){}


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
    //uint32_t len = trans.get_data_length();

    // Enforce register alignment rules
    // Treat address as offset (since TB binds directly to MC)
    // if (addr + len > REG_SPACE || len != 4 || (addr & 0x3)) {
    //     trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
    //     return;
    // }

    // Route to appropriate register handler
    if (addr < 0x20) {
        ctrl_handler(trans, delay);  // 0x00-0x1F: Control registers

    // Is bulk data transfer to CIM data region
    } else if (addr >= CIM_DATA_REGION) {
        dma_handler(trans, delay);

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
    auto len  = trans.get_data_length();

    uint32_t data = 0;
    // WRITE, write to specified registers based on what the CPU requests
    if (cmd == tlm::TLM_WRITE_COMMAND) {
        
        std::memcpy(&data, ptr, sizeof(uint32_t)); // Copy received data into data variable for assignment
        
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

    // Model register access latency
    delay += timing::CTRL_REG;
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

/**
 * Handle bulk transfer of data to/from SRAM (Used for DMA transactions)
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
 */
void Mem_Controller::dma_handler(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {

    /*  
        Do work in burst mode: https://www.geeksforgeeks.org/computer-organization-architecture/direct-memory-access-dma-controller-in-computer-architecture/

            In Burst Mode, the DMA controller takes full control of the system bus and transfers the entire block of data in one go.
            The bus is not handed back to the CPU until the entire data transfer is complete.
            This mode is efficient for large data transfers but can delay CPU operations.

        Client loads data into memory
        DMA transfer request:
        1. Set source addr reg
            sc_dev gets the source address (QEMU guest memory address), stores in sc_dev's dma_src register
        2. Set dest addr reg
            sc_dev gets the destination address (CIM SRAM memory address (e.g. WEIGHT_BASE_ADDR)), stores in sc_dev's dma_dst register
        3. Set len reg
            sc_dev gets the number of bytes to read (4KB max), stores in sc_dev's dma_len register
        4. Set start reg
            sc_dev_dma starts running
                QEMU DMA receives start signal and reads client memory pointed to by dma_src into temp buffer
                Sends block of data, dest, and len over socket to bridge
            
            (SystemC land)
            Bridge packages message as TLM transaction, sending the block of data to controller
            Memory controller forwards transaction to SRAM (sending data length and data pointer)
     */

    auto cmd  = trans.get_command();
    auto addr = static_cast<uint32_t>(trans.get_address());  // Destinaton address
    auto ptr  = trans.get_data_ptr();                        // Source data
    auto len  = trans.get_data_length();                     // Data length

    // Validate SRAM address bounds
    if (addr + len > SRAM_SIZE) {
        fprintf(stderr, "   DMA: ERROR - SRAM overflow error.");
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    // Validate transfer parameters
    if (len == 0) {
        fprintf(stderr, "   DMA: ERROR - len (%d) is 0\n", len);
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    // Write chunk to destination address
    if (cmd == tlm::TLM_WRITE_COMMAND) {
        sc_core::sc_time sram_delay = sc_core::SC_ZERO_TIME;

        tlm::tlm_generic_payload write_trans;
        prepare_sram_transaction(write_trans, tlm::TLM_WRITE_COMMAND, addr, ptr, len);
        i_sram_socket->b_transport(write_trans, sram_delay); // Don't add SRAM delay to DMA transfer
        
        if (write_trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
            printf("DMA: ERROR - Bad TLM response from SRAM (write): %s\n", write_trans.get_response_string().c_str());
            return;
        }

    // Read chunk from destination address
    } else if (cmd == tlm::TLM_READ_COMMAND) {
        sc_core::sc_time sram_delay = sc_core::SC_ZERO_TIME;

        tlm::tlm_generic_payload read_trans;
        prepare_sram_transaction(read_trans, tlm::TLM_READ_COMMAND, addr, ptr, len);
        i_sram_socket->b_transport(read_trans, sram_delay); // Don't add SRAM delay to DMA transfer
        
        if (read_trans.get_response_status() != tlm::TLM_OK_RESPONSE) {
            printf("DMA: ERROR - Bad TLM response from SRAM (read): %s\n", read_trans.get_response_string().c_str());
            return;
        }

    // ERROR
    } else {
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }

    // Model DMA access latency
    delay += timing::dma_transfer_time(len);
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
    }
}