static void pvideo_vga_invalidate(NV2AState *d)
{
    int y1 = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT],
                      NV_PVIDEO_POINT_OUT_Y);
    int y2 = y1 + GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT],
                           NV_PVIDEO_SIZE_OUT_HEIGHT);
    NV2A_DPRINTF("pvideo_vga_invalidate %d %d\n", y1, y2);
    vga_invalidate_scanlines(&d->vga, y1, y2);
}

uint64_t pvideo_read(void *opaque,
                            hwaddr addr, unsigned int size)
{
    NV2AState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    case NV_PVIDEO_STOP:
        r = 0;
        break;
    default:
        r = d->pvideo.regs[addr];
        break;
    }

    reg_log_read(NV_PVIDEO, addr, r);
    return r;
}

void pvideo_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned int size)
{
    NV2AState *d = opaque;

    reg_log_write(NV_PVIDEO, addr, val);

    switch (addr) {
    case NV_PVIDEO_BUFFER:
        d->pvideo.regs[addr] = val;
        // d->vga.enable_overlay = true;
        pvideo_vga_invalidate(d);
        break;
    case NV_PVIDEO_STOP:
        d->pvideo.regs[NV_PVIDEO_BUFFER] = 0;
        // d->vga.enable_overlay = false;
        pvideo_vga_invalidate(d);
        break;
    default:
        d->pvideo.regs[addr] = val;
        break;
    }
}


