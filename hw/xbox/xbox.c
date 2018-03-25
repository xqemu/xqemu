/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/i386/pc.h"
#include "hw/i386/apic.h"
#include "hw/smbios/smbios.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_ids.h"
#include "hw/usb.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/ide.h"
#include "sysemu/kvm.h"
#include "hw/kvm/clock.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "sysemu/arch_init.h"
#include "hw/i2c/smbus.h"
#include "hw/xen/xen.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/acpi/acpi.h"
#include "cpu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#ifdef CONFIG_XEN
#include <xen/hvm/hvm_info_table.h>
#include "hw/xen/xen_pt.h"
#endif
#include "migration/global_state.h"
#include "migration/misc.h"
#include "kvm_i386.h"
#include "sysemu/numa.h"

#include "hw/timer/mc146818rtc.h"
#include "xbox_pci.h"
#include "smbus.h"

#define MAX_IDE_BUS 2

// static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
// static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
// static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };

// XBOX_TODO: Should be passed in through configuration
/* bunnie's eeprom */
const uint8_t default_eeprom[] = {
    0xe3, 0x1c, 0x5c, 0x23, 0x6a, 0x58, 0x68, 0x37,
    0xb7, 0x12, 0x26, 0x6c, 0x99, 0x11, 0x30, 0xd1,
    0xe2, 0x3e, 0x4d, 0x56, 0xf7, 0x73, 0x2b, 0x73,
    0x85, 0xfe, 0x7f, 0x0a, 0x08, 0xef, 0x15, 0x3c,
    0x77, 0xee, 0x6d, 0x4e, 0x93, 0x2f, 0x28, 0xee,
    0xf8, 0x61, 0xf7, 0x94, 0x17, 0x1f, 0xfc, 0x11,
    0x0b, 0x84, 0x44, 0xed, 0x31, 0x30, 0x35, 0x35,
    0x38, 0x31, 0x31, 0x31, 0x34, 0x30, 0x30, 0x33,
    0x00, 0x50, 0xf2, 0x4f, 0x65, 0x52, 0x00, 0x00,
    0x0a, 0x1e, 0x35, 0x33, 0x71, 0x85, 0x31, 0x4d,
    0x59, 0x12, 0x38, 0x48, 0x1c, 0x91, 0x53, 0x60,
    0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x61, 0x57, 0xfb, 0x2c, 0x01, 0x00, 0x00,
    0x45, 0x53, 0x54, 0x00, 0x45, 0x44, 0x54, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0x05, 0x00, 0x02, 0x04, 0x01, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xc4, 0xff, 0xff, 0xff,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void xbox_memory_init(PCMachineState *pcms,
                             MemoryRegion *system_memory,
                             MemoryRegion *rom_memory,
                             MemoryRegion **ram_memory)
{
    // int linux_boot, i;
    MemoryRegion *ram;//, *option_rom_mr;
    MemoryRegion *ram_below_4g;//, *ram_above_4g;
    // FWCfgState *fw_cfg;
    MachineState *machine = MACHINE(pcms);
    // PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);

    pcms->below_4g_mem_size = 256 * 0x100000;
    assert(machine->ram_size <= pcms->below_4g_mem_size);

    // linux_boot = (machine->kernel_filename != NULL);

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    ram = g_malloc(sizeof(*ram));
    memory_region_allocate_system_memory(ram, NULL, "pc.ram",
                                         machine->ram_size);
    *ram_memory = ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", ram,
                             0, pcms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);
#if 0
    e820_add_entry(0, pcms->below_4g_mem_size, E820_RAM);
    if (pcms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g", ram,
                                 pcms->below_4g_mem_size,
                                 pcms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, pcms->above_4g_mem_size, E820_RAM);
    }

    if (!pcmc->has_reserved_memory &&
        (machine->ram_slots ||
         (machine->maxram_size > machine->ram_size))) {
        MachineClass *mc = MACHINE_GET_CLASS(machine);

        error_report("\"-memory 'slots|maxmem'\" is not supported by: %s",
                     mc->name);
        exit(EXIT_FAILURE);
    }

    /* initialize hotplug memory address space */
    if (pcmc->has_reserved_memory &&
        (machine->ram_size < machine->maxram_size)) {
        ram_addr_t hotplug_mem_size =
            machine->maxram_size - machine->ram_size;

        if (machine->ram_slots > ACPI_MAX_RAM_SLOTS) {
            error_report("unsupported amount of memory slots: %"PRIu64,
                         machine->ram_slots);
            exit(EXIT_FAILURE);
        }

        if (QEMU_ALIGN_UP(machine->maxram_size,
                          TARGET_PAGE_SIZE) != machine->maxram_size) {
            error_report("maximum memory size must by aligned to multiple of "
                         "%d bytes", TARGET_PAGE_SIZE);
            exit(EXIT_FAILURE);
        }

        pcms->hotplug_memory.base =
            ROUND_UP(0x100000000ULL + pcms->above_4g_mem_size, 1ULL << 30);

        if (pcmc->enforce_aligned_dimm) {
            /* size hotplug region assuming 1G page max alignment per slot */
            hotplug_mem_size += (1ULL << 30) * machine->ram_slots;
        }

        if ((pcms->hotplug_memory.base + hotplug_mem_size) <
            hotplug_mem_size) {
            error_report("unsupported amount of maximum memory: " RAM_ADDR_FMT,
                         machine->maxram_size);
            exit(EXIT_FAILURE);
        }

        memory_region_init(&pcms->hotplug_memory.mr, OBJECT(pcms),
                           "hotplug-memory", hotplug_mem_size);
        memory_region_add_subregion(system_memory, pcms->hotplug_memory.base,
                                    &pcms->hotplug_memory.mr);
    }

    /* Initialize PC system firmware */
    pc_system_firmware_init(rom_memory, !pcmc->pci_enabled);

    option_rom_mr = g_malloc(sizeof(*option_rom_mr));
    memory_region_init_ram(option_rom_mr, NULL, "pc.rom", PC_ROM_SIZE,
                           &error_fatal);
    if (pcmc->pci_enabled) {
        memory_region_set_readonly(option_rom_mr, true);
    }
    memory_region_add_subregion_overlap(rom_memory,
                                        PC_ROM_MIN_VGA,
                                        option_rom_mr,
                                        1);

    fw_cfg = bochs_bios_init(&address_space_memory, pcms);

    rom_set_fw(fw_cfg);

    if (pcmc->has_reserved_memory && pcms->hotplug_memory.base) {
        uint64_t *val = g_malloc(sizeof(*val));
        PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
        uint64_t res_mem_end = pcms->hotplug_memory.base;

        if (!pcmc->broken_reserved_end) {
            res_mem_end += memory_region_size(&pcms->hotplug_memory.mr);
        }
        *val = cpu_to_le64(ROUND_UP(res_mem_end, 0x1ULL << 30));
        fw_cfg_add_file(fw_cfg, "etc/reserved-memory-end", val, sizeof(*val));
    }

    if (linux_boot) {
        load_linux(pcms, fw_cfg);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    pcms->fw_cfg = fw_cfg;

    /* Init default IOAPIC address space */
    pcms->ioapic_as = &address_space_memory;
#endif


    int ret;
    char *filename;
    int bios_size;
    MemoryRegion *bios;

    MemoryRegion *map_bios;
    uint32_t map_loc;


    /* Load the bios. (mostly from pc_sysfw)
     * Can't use it verbatim, since we need the bios repeated
     * over top 1MB of memory.
     */
    if (bios_name == NULL) {
        bios_name = "bios.bin";
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, NULL, "xbox.bios", bios_size, &error_fatal);
    memory_region_set_readonly(bios, true);
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
bios_error:
        fprintf(stderr, "qemu: could not load xbox BIOS '%s'\n", bios_name);
        exit(1);
    }
    if (filename) {
        g_free(filename);
    }

    /* map the bios repeated at the top of memory */
    for (map_loc=(uint32_t)(-bios_size); map_loc >= 0xff000000; map_loc-=bios_size) {
        map_bios = g_malloc(sizeof(*map_bios));
        // had to add a name here otherwise it crashes when trying to go to parent node.. go figure
        memory_region_init_alias(map_bios, NULL, "pci-bios", bios, 0, bios_size);
        memory_region_add_subregion(rom_memory, map_loc, map_bios);
        memory_region_set_readonly(map_bios, true);
    }
}


/* PC hardware initialisation */
static void xbox_init(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);

    MemoryRegion *system_memory = get_system_memory();
    // MemoryRegion *system_io = get_system_io();

    int i;

    PCIBus *pci_bus;
    ISABus *isa_bus;

    int piix3_devfn = -1;

    qemu_irq *i8259;
    // qemu_irq smi_irq; // XBOX_TODO: SMM support?

    GSIState *gsi_state;

    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    BusState *idebus[MAX_IDE_BUS];
    ISADevice *rtc_state;
    // ISADevice *pit;

    MemoryRegion *ram_memory;
    MemoryRegion *pci_memory;
    MemoryRegion *rom_memory;

    I2CBus *smbus;
    PCIBus *agp_bus;

    printf("XBOX Starting :)\n");

    pc_cpus_init(pcms);

    if (kvm_enabled() && pcmc->kvmclock_enabled) {
        kvmclock_create();
    }

    pci_memory = g_new(MemoryRegion, 1);
    memory_region_init(pci_memory, NULL, "pci", UINT64_MAX);
    rom_memory = pci_memory;

    pc_guest_info_init(pcms);

    /* allocate ram and load rom/bios */
    xbox_memory_init(pcms, system_memory, rom_memory, &ram_memory);

    gsi_state = g_malloc0(sizeof(*gsi_state));
    // if (kvm_ioapic_in_kernel()) {
    //     kvm_pc_setup_irq_routing(pcmc->pci_enabled);
    //     pcms->gsi = qemu_allocate_irqs(kvm_pc_gsi_handler, gsi_state,
    //                                    GSI_NUM_PINS);
    // } else {
        pcms->gsi = qemu_allocate_irqs(gsi_handler, gsi_state, GSI_NUM_PINS);
    // }

    // pci_bus = i440fx_init(host_type,
    //                       pci_type,
    //                       &i440fx_state, &piix3_devfn, &isa_bus, pcms->gsi,
    //                       system_memory, system_io, machine->ram_size,
    //                       pcms->below_4g_mem_size,
    //                       pcms->above_4g_mem_size,
    //                       pci_memory, ram_memory);
    xbox_pci_init(pcms->gsi,
                  get_system_memory(), get_system_io(),
                  pci_memory, ram_memory,
                  &pci_bus,
                  &isa_bus,
                  &smbus,
                  &agp_bus);

    pcms->bus = pci_bus;

    isa_bus_irqs(isa_bus, pcms->gsi);

    if (kvm_pic_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    // } else if (xen_enabled()) {
    //     i8259 = xen_interrupt_controller_init();
    } else {
        i8259 = i8259_init(isa_bus, pc_allocate_cpu_irq());
    }

    for (i = 0; i < ISA_NUM_IRQS; i++) {
        gsi_state->i8259_irq[i] = i8259[i];
    }
    g_free(i8259);
    // if (pcmc->pci_enabled) {
    //     ioapic_init_gsi(gsi_state, "i440fx");
    // }

    pc_register_ferr_irq(pcms->gsi[13]);

    // pc_vga_init(isa_bus, pcmc->pci_enabled ? pci_bus : NULL);

    assert(pcms->vmport != ON_OFF_AUTO__MAX);
    if (pcms->vmport == ON_OFF_AUTO_AUTO) {
        pcms->vmport = xen_enabled() ? ON_OFF_AUTO_OFF : ON_OFF_AUTO_ON;
    }

    /* init basic PC hardware */
    pcms->pit = 1; // XBOX_FIXME: What's the right way to do this?
    pc_basic_device_init(isa_bus, pcms->gsi, &rtc_state, true,
                         (pcms->vmport != ON_OFF_AUTO_ON), pcms->pit, 0x4);

    pc_nic_init(pcmc, isa_bus, pci_bus);

    ide_drive_get(hd, ARRAY_SIZE(hd));
    // if (pcmc->pci_enabled) {
        PCIDevice *dev;
        // if (xen_enabled()) {
            // dev = pci_piix3_xen_ide_init(pci_bus, hd, piix3_devfn + 1);
        // } else {
            dev = pci_piix3_ide_init(pci_bus, hd, PCI_DEVFN(9, 0));
        // }
        idebus[0] = qdev_get_child_bus(&dev->qdev, "ide.0");
        idebus[1] = qdev_get_child_bus(&dev->qdev, "ide.1");


    printf("%s: %d\n", __func__, __LINE__);
    // } else {
    //     for(i = 0; i < MAX_IDE_BUS; i++) {
    //         ISADevice *dev;
    //         char busname[] = "ide.0";
    //         dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
    //                            ide_irq[i],
    //                            hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
    //         /*
    //          * The ide bus name is ide.0 for the first bus and ide.1 for the
    //          * second one.
    //          */
    //         busname[4] = '0' + i;
    //         idebus[i] = qdev_get_child_bus(DEVICE(dev), busname);
    //     }
    // }

    pc_cmos_init(pcms, idebus[0], idebus[1], rtc_state);

    // xbox bios wants this bit pattern set to mark the data as valid
    uint8_t bits = 0x55;
    for (i = 0x10; i < 0x70; i++) {
        rtc_set_memory(rtc_state, i, bits);
        bits = ~bits;
    }
    bits = 0x55;
    for (i = 0x80; i < 0x100; i++) {
        rtc_set_memory(rtc_state, i, bits);
        bits = ~bits;
    }

    /* smbus devices */
    uint8_t *eeprom_buf = g_malloc0(256);
    memcpy(eeprom_buf, default_eeprom, 256);
    smbus_eeprom_init_single(smbus, 0x54, eeprom_buf);

    smbus_xbox_smc_init(smbus, 0x10);
    smbus_cx25871_init(smbus, 0x45);
    smbus_adm1032_init(smbus, 0x4c);

#if 0
    if (pcmc->pci_enabled && machine_usb(machine)) {
        pci_create_simple(pci_bus, piix3_devfn + 2, "piix3-usb-uhci");
    }

    if (pcmc->pci_enabled && acpi_enabled) {
        DeviceState *piix4_pm;
        I2CBus *smbus;

        smi_irq = qemu_allocate_irq(pc_acpi_smi_interrupt, first_cpu, 0);
        /* TODO: Populate SPD eeprom data.  */
        smbus = piix4_pm_init(pci_bus, piix3_devfn + 3, 0xb100,
                              pcms->gsi[9], smi_irq,
                              pc_machine_is_smm_enabled(pcms),
                              &piix4_pm);
        smbus_eeprom_init(smbus, 8, NULL, 0);

        object_property_add_link(OBJECT(machine), PC_MACHINE_ACPI_DEVICE_PROP,
                                 TYPE_HOTPLUG_HANDLER,
                                 (Object **)&pcms->acpi_dev,
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE, &error_abort);
        object_property_set_link(OBJECT(machine), OBJECT(piix4_pm),
                                 PC_MACHINE_ACPI_DEVICE_PROP, &error_abort);
    }

    if (pcms->acpi_nvdimm_state.is_enabled) {
        nvdimm_init_acpi_state(&pcms->acpi_nvdimm_state, system_io,
                               pcms->fw_cfg, OBJECT(pcms));
    }
#endif

    /* USB */
    PCIDevice *usb1 = pci_create(pci_bus, PCI_DEVFN(3, 0), "pci-ohci");
    qdev_prop_set_uint32(&usb1->qdev, "num-ports", 4);
    qdev_init_nofail(&usb1->qdev);

    PCIDevice *usb0 = pci_create(pci_bus, PCI_DEVFN(2, 0), "pci-ohci");
    qdev_prop_set_uint32(&usb0->qdev, "num-ports", 4);
    qdev_init_nofail(&usb0->qdev);

    /* Ethernet! */
#if 0
    PCIDevice *nvnet = pci_create(pci_bus, PCI_DEVFN(4, 0), "nvnet");

    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        qemu_check_nic_model(nd, "nvnet");
        qdev_set_nic_properties(&nvnet->qdev, nd);
        qdev_init_nofail(&nvnet->qdev);
    }
#endif

    /* APU! */
    // PCIDevice *apu = 
    pci_create_simple(pci_bus, PCI_DEVFN(5, 0), "mcpx-apu");

    /* ACI! */
    // PCIDevice *aci =
    pci_create_simple(pci_bus, PCI_DEVFN(6, 0), "mcpx-aci");

    /* GPU! */
    // nv2a_init(agp_bus, PCI_DEVFN(0, 0), ram_memory);

    printf("%s: %d\n", __func__, __LINE__);
}


static void xbox_machine_options(MachineClass *m)
{
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);
    m->desc = "Microsoft Xbox";
    m->max_cpus = 1;
    m->option_rom_has_mr = true;
    m->rom_file_has_mr = false;


    m->no_floppy = 1,
    m->no_cdrom = 1,
    m->no_sdcard = 1,

    pcmc->pci_enabled = true;
    pcmc->has_acpi_build = false;
    pcmc->smbios_defaults = false;
    pcmc->gigabyte_align = false;
    pcmc->smbios_legacy_mode = true;
    pcmc->has_reserved_memory = false;
    pcmc->default_nic_model = "ne2k_isa";
    m->default_cpu_type = X86_CPU_TYPE_NAME("486");
}

DEFINE_PC_MACHINE(xbox, "xbox", xbox_init,
                  xbox_machine_options);
