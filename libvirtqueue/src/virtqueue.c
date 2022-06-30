/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <utils/util.h>
#include <virtqueue.h>

void virtqueue_init_driver(virtqueue_driver_t *vq, size_t queue_len, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    if (!IS_POWER_OF_2(queue_len)) {
        ZF_LOGE("Invalid queue_len: %d, must be a power of 2.", queue_len);
    }
    vq->free_desc_head = 0;
    vq->queue_len = queue_len;
    vq->u_ring_last_seen = vq->queue_len - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
    virtqueue_init_desc_table(desc, vq->queue_len);
    virtqueue_init_avail_ring(avail_ring);
    virtqueue_init_used_ring(used_ring);

}

void virtqueue_init_device(virtqueue_device_t *vq, size_t queue_len, vq_vring_avail_t *avail_ring,
                           vq_vring_used_t *used_ring, vq_vring_desc_t *desc, void (*notify)(void),
                           void *cookie)
{
    if (!IS_POWER_OF_2(queue_len)) {
        ZF_LOGE("Invalid queue_len: %d, must be a power of 2.", queue_len);
    }
    vq->queue_len = queue_len;
    vq->a_ring_last_seen = vq->queue_len - 1;
    vq->avail_ring = avail_ring;
    vq->used_ring = used_ring;
    vq->desc_table = desc;
    vq->notify = notify;
    vq->cookie = cookie;
}

void virtqueue_init_desc_table(vq_vring_desc_t *table, size_t queue_len)
{
    for (unsigned i = 0; i < queue_len; i++) {
        table[i] = (vq_vring_desc_t) {
            /* All fields not mentioned are set to zero. */
            .next = i + 1
        };
    }
}

void virtqueue_init_avail_ring(vq_vring_avail_t *ring)
{
    ring->flags = 0;
    ring->idx = 0;
}

void virtqueue_init_used_ring(vq_vring_used_t *ring)
{
    ring->flags = 0;
    ring->idx = 0;
}

static size_t vq_add_desc(virtqueue_driver_t *vq, void *buf, size_t len,
                          vq_flags_t flag, int prev)
{
    size_t head = vq->free_desc_head;
    if (head == vq->queue_len) {
        return head;
    }

    vq_vring_desc_t *desc = &vq->desc_table[head];
    vq->free_desc_head = desc->next;

    desc->addr = (uintptr_t)buf;
    desc->len = len;
    desc->flags = flag;
    desc->next = vq->queue_len;

    if (prev < vq->queue_len) {
        desc = vq->desc_table + prev;
        desc->next = head;
    }

    return head;
}

static size_t vq_pop_desc(virtqueue_driver_t *vq, size_t idx,
                          void **buf, size_t *len, vq_flags_t *flag)
{
    vq_vring_desc_t *desc = &vq->desc_table[idx];

    // casting integers to pointers directly is not allowed, must cast the
    // integer to a uintptr_t first
    *buf = (void *)(uintptr_t)(desc->addr);
    *len = desc->len;
    *flag = desc->flags;

    size_t next = desc->next;
    desc->next = vq->free_desc_head;
    vq->free_desc_head = idx;

    return next;
}

int virtqueue_add_available_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj,
                                void *buf, size_t len, vq_flags_t flag)
{
    size_t idx = vq_add_desc(vq, buf, len, flag, obj->cur);

    /* If descriptor table full */
    if ((idx = vq_add_desc(vq, buf, len, flag, obj->cur)) == vq->queue_len) {
        return 0;
    }
    obj->cur = idx;

    /* If this is the first buffer in the descriptor chain */
    if (obj->first >= vq->queue_len) {
        obj->first = idx;
        struct vq_vring_avail *ring = vq->avail_ring;
        struct vq_vring_avail_elem *e = &ring->ring[ring->idx];
        e->id = idx;
        ring->idx = (ring->idx + 1) & (vq->queue_len - 1);
    }
    return 1;
}

int virtqueue_get_used_buf(virtqueue_driver_t *vq, virtqueue_ring_object_t *obj, size_t *len)
{
    struct vq_vring_used *ring = vq->used_ring;
    size_t next = (vq->u_ring_last_seen + 1) & (vq->queue_len - 1);
    if (next == ring->idx) {
        return 0;
    }
    vq->u_ring_last_seen = next;
    struct vq_vring_used_elem *e = &ring->ring[next];
    obj->first = e->id;
    obj->cur = e->id;
    *len = e->len;
    return 1;
}

int virtqueue_add_used_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj, size_t len)
{
    /* The len passed here is the length of the data. Obviously, the length
     * can't be greater than the the space available in all chained buffers of
     * the ring object. We have a sanity check here for debug builds, but it's
     * not really our concern here. The driver must check this when it takes
     * data out of the queue and then decide how to handle this.
     * There could be more buffers chained in the ring object then actually
     * necessary to hold data. Especially, the caller can pass 0 here if there
     * no data is in the buffers at all, because it is returning something to
     * the queue to be recycled.
     */
    struct vq_vring_used *ring = vq->used_ring;
    struct vq_vring_used_elem *e = &ring->ring[ring->idx];
    e->id = robj->first;
    e->len = len;
    ring->idx = (ring->idx + 1) & (vq->queue_len - 1);
    return 1;
}

int virtqueue_get_available_buf(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    struct vq_vring_avail *ring = vq->avail_ring;
    size_t next = (vq->a_ring_last_seen + 1) & (vq->queue_len - 1);
    if (next == ring->idx) {
        return 0;
    }
    struct vq_vring_avail_elem *e = &ring->ring[next];
    robj->first = e->id;
    robj->cur = robj->first;
    vq->a_ring_last_seen = next;
    return 1;
}

void virtqueue_init_ring_object(virtqueue_ring_object_t *obj)
{
    obj->cur = (size_t)(-1);
    obj->first = (size_t)(-1);
}

size_t virtqueue_scattered_available_size(virtqueue_device_t *vq, virtqueue_ring_object_t *robj)
{
    size_t ret = 0;
    size_t cur = robj->first;

    while (cur < vq->queue_len) {
        vq_vring_desc_t *desc = &vq->desc_table[cur];
        ret += desc->len;
        cur = desc->next;
    }
    return ret;
}

int virtqueue_gather_available(virtqueue_device_t *vq, virtqueue_ring_object_t *robj,
                               void **buf, size_t *len, vq_flags_t *flag)
{
    size_t idx = robj->cur;

    if (idx >= vq->queue_len) {
        return 0;
    }

    vq_vring_desc_t *desc = &vq->desc_table[idx];

    // casting integers to pointers directly is not allowed, must cast the
    // integer to a uintptr_t first
    *buf = (void *)(uintptr_t)(desc->addr);
    *len = desc->len;
    *flag = desc->flags;

    robj->cur = desc->next;

    return 1;
}

int virtqueue_gather_used(virtqueue_driver_t *vq, virtqueue_ring_object_t *robj,
                          void **buf, size_t *len, vq_flags_t *flag)
{
    if (robj->cur >= vq->queue_len) {
        return 0;
    }
    robj->cur = vq_pop_desc(vq, robj->cur, buf, len, flag);
    return 1;
}
