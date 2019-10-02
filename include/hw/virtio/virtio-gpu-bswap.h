/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_GPU_BSWAP_H
#define HW_VIRTIO_GPU_BSWAP_H

#include "qemu/bswap.h"

static inline void
virtio_gpu_ctrl_hdr_bswap(struct virtio_gpu_ctrl_hdr *hdr)
{
    le32_to_cpus(&hdr->type);
    le32_to_cpus(&hdr->flags);
    le64_to_cpus(&hdr->fence_id);
    le32_to_cpus(&hdr->ctx_id);
    le32_to_cpus(&hdr->padding);
}

static inline void
virtio_gpu_bswap_32(void *ptr, size_t size)
{
#ifdef HOST_WORDS_BIGENDIAN

    size_t i;
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *) ptr;

    virtio_gpu_ctrl_hdr_bswap(hdr);

    i = sizeof(struct virtio_gpu_ctrl_hdr);
    while (i < size) {
        le32_to_cpus((uint32_t *)(ptr + i));
        i = i + sizeof(uint32_t);
    }

#endif
}

static inline void
virtio_gpu_t2d_bswap(struct virtio_gpu_transfer_to_host_2d *t2d)
{
    virtio_gpu_ctrl_hdr_bswap(&t2d->hdr);
    le32_to_cpus(&t2d->r.x);
    le32_to_cpus(&t2d->r.y);
    le32_to_cpus(&t2d->r.width);
    le32_to_cpus(&t2d->r.height);
    le64_to_cpus(&t2d->offset);
    le32_to_cpus(&t2d->resource_id);
    le32_to_cpus(&t2d->padding);
}

#endif
