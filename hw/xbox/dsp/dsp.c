/*
 * MCPX DSP emulator
 *
 * Copyright (c) 2015 espes
 *
 * Adapted from Hatari DSP M56001 emulation
 * (C) 2001-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#include "dsp_cpu.h"
#include "dsp_dma.h"

#include "dsp.h"

/* Defines */
#define BITMASK(x)  ((1<<(x))-1)
#define ARRAYSIZE(x) (int)(sizeof(x)/sizeof(x[0]))

#define INTERRUPT_ABORT_FRAME (1 << 0)
#define INTERRUPT_START_FRAME (1 << 1)
#define INTERRUPT_DMA_EOL (1 << 7)

#define DPRINTF(s, ...) printf(s, ## __VA_ARGS__)

struct DSPState {
    dsp_core_t core;
    DSPDMAState dma;
    int save_cycles;

    uint32_t interrupts;
};

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address);
static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value);

DSPState* dsp_init(void* scratch_rw_opaque, dsp_scratch_rw_func scratch_rw)
{
    DPRINTF("dsp_init\n");

    DSPState* dsp = (DSPState*)malloc(sizeof(DSPState));
    memset(dsp, 0, sizeof(*dsp));

    dsp->core.read_peripheral = read_peripheral;
    dsp->core.write_peripheral = write_peripheral;

    dsp->dma.core = &dsp->core;
    dsp->dma.scratch_rw_opaque = scratch_rw_opaque;
    dsp->dma.scratch_rw = scratch_rw;

    dsp_reset(dsp);

    return dsp;
}

void dsp_reset(DSPState* dsp)
{
    dsp56k_reset_cpu(&dsp->core);
    dsp->save_cycles = 0;
}

void dsp_destroy(DSPState* dsp)
{
    free(dsp);
}

static uint32_t read_peripheral(dsp_core_t* core, uint32_t address) {
    DSPState* dsp = container_of(core, DSPState, core);

    // printf("read_peripheral 0x%06x\n", address);

    uint32_t v = 0xababa;
    switch(address) {
    case 0xFFFFC5:
        v = dsp->interrupts;
        if (dsp->dma.eol) {
            v |= INTERRUPT_DMA_EOL;
        }
        break;
    case 0xFFFFD4:
        v = dsp_dma_read(&dsp->dma, DMA_NEXT_BLOCK);
        break;
    case 0xFFFFD5:
        v = dsp_dma_read(&dsp->dma, DMA_START_BLOCK);
        break;
    case 0xFFFFD6:
        v = dsp_dma_read(&dsp->dma, DMA_CONTROL);
        break;
    case 0xFFFFD7:
        v = dsp_dma_read(&dsp->dma, DMA_CONFIGURATION);
        break;
    }

    // printf(" -> 0x%06x\n", v);
    return v;
}

static void write_peripheral(dsp_core_t* core, uint32_t address, uint32_t value) {
    DSPState* dsp = container_of(core, DSPState, core);

    // printf("write_peripheral [0x%06x] = 0x%06x\n", address, value);

    switch(address) {
    case 0xFFFFC5:
        dsp->interrupts &= ~value;
        if (value & INTERRUPT_DMA_EOL) {
            dsp->dma.eol = false;
        }
        break;
    case 0xFFFFD4:
        dsp_dma_write(&dsp->dma, DMA_NEXT_BLOCK, value);
        break;
    case 0xFFFFD5:
        dsp_dma_write(&dsp->dma, DMA_START_BLOCK, value);
        break;
    case 0xFFFFD6:
        dsp_dma_write(&dsp->dma, DMA_CONTROL, value);
        break;
    case 0xFFFFD7:
        dsp_dma_write(&dsp->dma, DMA_CONFIGURATION, value);
        break;
    }
}


void dsp_step(DSPState* dsp)
{
    dsp56k_execute_instruction(&dsp->core);
}

void dsp_run(DSPState* dsp, int cycles)
{
    dsp->save_cycles += cycles;

    if (dsp->save_cycles <= 0) return;

    // if (unlikely(bDspDebugging)) {
    //     while (dsp->core.save_cycles > 0)
    //     {
    //         dsp56k_execute_instruction();
    //         dsp->core.save_cycles -= dsp->core.instr_cycle;
    //         DebugDsp_Check();
    //     }
    // } else {
    //  printf("--> %d\n", dsp->core.save_cycles);
    while (dsp->save_cycles > 0)
    {
        dsp56k_execute_instruction(&dsp->core);
        dsp->save_cycles -= dsp->core.instr_cycle;
    }

} 

void dsp_bootstrap(DSPState* dsp)
{
    // scratch memory is dma'd in to pram by the bootrom
    dsp->dma.scratch_rw(dsp->dma.scratch_rw_opaque,
        (uint8_t*)dsp->core.pram, 0, 0x800*4, false);
}

void dsp_start_frame(DSPState* dsp)
{
    dsp->interrupts |= INTERRUPT_START_FRAME;
}

/**
 * Disassemble DSP code between given addresses, return next PC address
 */
uint32_t dsp_disasm_address(DSPState* dsp, FILE *out, uint32_t lowerAdr, uint32_t UpperAdr)
{
    uint32_t dsp_pc;

    for (dsp_pc=lowerAdr; dsp_pc<=UpperAdr; dsp_pc++) {
        dsp_pc += dsp56k_execute_one_disasm_instruction(&dsp->core, out, dsp_pc);
    }
    return dsp_pc;
}

uint32_t dsp_read_memory(DSPState* dsp, char space, uint32_t address)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(false);
    }

    return dsp56k_read_memory(&dsp->core, space_id, address);
}

void dsp_write_memory(DSPState* dsp, char space, uint32_t address, uint32_t value)
{
    int space_id;

    switch (space) {
    case 'X':
        space_id = DSP_SPACE_X;
        break;
    case 'Y':
        space_id = DSP_SPACE_Y;
        break;
    case 'P':
        space_id = DSP_SPACE_P;
        break;
    default:
        assert(false);
    }

    dsp56k_write_memory(&dsp->core, space_id, address, value);
}

/**
 * Output memory values between given addresses in given DSP address space.
 * Return next DSP address value.
 */
uint32_t dsp_disasm_memory(DSPState* dsp, uint32_t dsp_memdump_addr, uint32_t dsp_memdump_upper, char space)
{
    uint32_t mem, value;

    for (mem = dsp_memdump_addr; mem <= dsp_memdump_upper; mem++) {
        value = dsp_read_memory(dsp, space, mem);
        printf("%04x  %06x\n", mem, value);
    }
    return dsp_memdump_upper+1;
}

/**
 * Show information on DSP core state which isn't
 * shown by any of the other commands (dd, dm, dr).
 */
void dsp_info(DSPState* dsp)
{
    int i, j;
    const char *stackname[] = { "SSH", "SSL" };

    printf("DSP core information:\n");

    for (i = 0; i < ARRAYSIZE(stackname); i++) {
        printf("- %s stack:", stackname[i]);
        for (j = 0; j < ARRAYSIZE(dsp->core.stack[0]); j++) {
            printf(" %04x", dsp->core.stack[i][j]);
        }
        printf("\n");
    }

    printf("- Interrupt IPL:");
    for (i = 0; i < ARRAYSIZE(dsp->core.interrupt_ipl); i++) {
        printf(" %04x", dsp->core.interrupt_ipl[i]);
    }
    printf("\n");

    printf("- Pending ints: ");
    for (i = 0; i < ARRAYSIZE(dsp->core.interrupt_is_pending); i++) {
        printf(" %04hx", dsp->core.interrupt_is_pending[i]);
    }
    printf("\n");
}

/**
 * Show DSP register contents
 */
void dsp_print_registers(DSPState* dsp)
{
    uint32_t i;

    printf("A: A2: %02x  A1: %06x  A0: %06x\n",
        dsp->core.registers[DSP_REG_A2], dsp->core.registers[DSP_REG_A1], dsp->core.registers[DSP_REG_A0]);
    printf("B: B2: %02x  B1: %06x  B0: %06x\n",
        dsp->core.registers[DSP_REG_B2], dsp->core.registers[DSP_REG_B1], dsp->core.registers[DSP_REG_B0]);
    
    printf("X: X1: %06x  X0: %06x\n", dsp->core.registers[DSP_REG_X1], dsp->core.registers[DSP_REG_X0]);
    printf("Y: Y1: %06x  Y0: %06x\n", dsp->core.registers[DSP_REG_Y1], dsp->core.registers[DSP_REG_Y0]);

    for (i=0; i<8; i++) {
        printf("R%01x: %04x   N%01x: %04x   M%01x: %04x\n", 
            i, dsp->core.registers[DSP_REG_R0+i],
            i, dsp->core.registers[DSP_REG_N0+i],
            i, dsp->core.registers[DSP_REG_M0+i]);
    }

    printf("LA: %04x   LC: %04x   PC: %04x\n", dsp->core.registers[DSP_REG_LA], dsp->core.registers[DSP_REG_LC], dsp->core.pc);
    printf("SR: %04x  OMR: %02x\n", dsp->core.registers[DSP_REG_SR], dsp->core.registers[DSP_REG_OMR]);
    printf("SP: %02x    SSH: %04x  SSL: %04x\n", 
        dsp->core.registers[DSP_REG_SP], dsp->core.registers[DSP_REG_SSH], dsp->core.registers[DSP_REG_SSL]);
}
