/*
 *  PowerPC MMU stub handling for user mode emulation
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (c) 2013 David Gibson, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"


bool ppc_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int exception, error_code;

    if (access_type == MMU_INST_FETCH) {
        exception = POWERPC_EXCP_ISI;
        error_code = 0x40000000;
    } else {
        exception = POWERPC_EXCP_DSI;
        error_code = 0x40000000;
        if (access_type == MMU_DATA_STORE) {
            error_code |= 0x02000000;
        }
        env->spr[SPR_DAR] = address;
        env->spr[SPR_DSISR] = error_code;
    }
    cs->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit_restore(cs, retaddr);
}
