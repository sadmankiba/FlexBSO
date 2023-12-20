/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#include "bdev_raid.h"

#include "spdk/likely.h"
#include "spdk/log.h"

struct raid1_info {
	/* The parent raid bdev */
	struct raid_bdev *raid_bdev;
};

struct raid1_io_channel {
	/* Array of per-base_bdev counters of outstanding read blocks on this channel */
	uint64_t read_blocks_outstanding[0];
};

static void
raid1_channel_inc_read_counters(struct raid_bdev_io_channel *raid_ch, uint8_t idx,
				uint64_t num_blocks)
{
	struct raid1_io_channel *raid1_ch = spdk_io_channel_get_ctx(raid_ch->module_channel);

	assert(raid1_ch->read_blocks_outstanding[idx] <= UINT64_MAX - num_blocks);
	raid1_ch->read_blocks_outstanding[idx] += num_blocks;
}

static void
raid1_channel_dec_read_counters(struct raid_bdev_io_channel *raid_ch, uint8_t idx,
				uint64_t num_blocks)
{
	struct raid1_io_channel *raid1_ch = spdk_io_channel_get_ctx(raid_ch->module_channel);

	assert(raid1_ch->read_blocks_outstanding[idx] >= num_blocks);
	raid1_ch->read_blocks_outstanding[idx] -= num_blocks;
}

static inline void
raid1_bdev_io_completion(struct spdk_bdev_io *bdev_io, bool success, struct raid_bdev_io *raid_io)
{
	spdk_bdev_free_io(bdev_io);

	raid_bdev_io_complete_part(raid_io, 1, success ?
				   SPDK_BDEV_IO_STATUS_SUCCESS :
				   SPDK_BDEV_IO_STATUS_FAILED);
}

static void
raid1_write_bdev_io_completion(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct raid_bdev_io *raid_io = cb_arg;

	raid1_bdev_io_completion(bdev_io, success, raid_io);
}

static void
raid1_read_bdev_io_completion(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct raid_bdev_io *raid_io = cb_arg;

	raid1_channel_dec_read_counters(raid_io->raid_ch, raid_io->base_bdev_io_submitted,
					spdk_bdev_io_from_ctx(raid_io)->u.bdev.num_blocks);

	raid1_bdev_io_completion(bdev_io, success, raid_io);
}

static void raid1_submit_rw_request(struct raid_bdev_io *raid_io);

static void
_raid1_submit_rw_request(void *_raid_io)
{
	struct raid_bdev_io *raid_io = _raid_io;

	raid1_submit_rw_request(raid_io);
}

static void
raid1_init_ext_io_opts(struct spdk_bdev_io *bdev_io, struct spdk_bdev_ext_io_opts *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->size = sizeof(*opts);
	opts->memory_domain = bdev_io->u.bdev.memory_domain;
	opts->memory_domain_ctx = bdev_io->u.bdev.memory_domain_ctx;
	opts->metadata = bdev_io->u.bdev.md_buf;
}

static uint8_t
raid1_channel_next_read_base_bdev(struct raid_bdev_io_channel *raid_ch)
{
	struct raid1_io_channel *raid1_ch = spdk_io_channel_get_ctx(raid_ch->module_channel);
	uint64_t read_blocks_min = UINT64_MAX;
	uint8_t idx = UINT8_MAX;
	uint8_t i;

	for (i = 0; i < raid_ch->num_channels; i++) {
		if (raid_ch->base_channel[i] != NULL &&
		    raid1_ch->read_blocks_outstanding[i] < read_blocks_min) {
			read_blocks_min = raid1_ch->read_blocks_outstanding[i];
			idx = i;
		}
	}

	return idx;
}

static int
raid1_submit_read_request(struct raid_bdev_io *raid_io)
{
	struct raid_bdev *raid_bdev = raid_io->raid_bdev;
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	struct raid_bdev_io_channel *raid_ch = raid_io->raid_ch;
	struct spdk_bdev_ext_io_opts io_opts;
	struct raid_base_bdev_info *base_info;
	struct spdk_io_channel *base_ch;
	uint64_t pd_lba, pd_blocks;
	uint8_t idx;
	int ret;

	pd_lba = bdev_io->u.bdev.offset_blocks;
	pd_blocks = bdev_io->u.bdev.num_blocks;

	idx = raid1_channel_next_read_base_bdev(raid_ch);
	if (spdk_unlikely(idx == UINT8_MAX)) {
		raid_bdev_io_complete(raid_io, SPDK_BDEV_IO_STATUS_FAILED);
		return 0;
	}

	base_info = &raid_bdev->base_bdev_info[idx];
	base_ch = raid_ch->base_channel[idx];

	raid_io->base_bdev_io_remaining = 1;

	raid1_init_ext_io_opts(bdev_io, &io_opts);
	ret = raid_bdev_readv_blocks_ext(base_info, base_ch,
					 bdev_io->u.bdev.iovs, bdev_io->u.bdev.iovcnt,
					 pd_lba, pd_blocks, raid1_read_bdev_io_completion,
					 raid_io, &io_opts);

	if (spdk_likely(ret == 0)) {
		raid1_channel_inc_read_counters(raid_ch, idx, pd_blocks);
		raid_io->base_bdev_io_submitted = idx;
	} else if (spdk_unlikely(ret == -ENOMEM)) {
		raid_bdev_queue_io_wait(raid_io, spdk_bdev_desc_get_bdev(base_info->desc),
					base_ch, _raid1_submit_rw_request);
		return 0;
	}

	return ret;
}

static int
raid1_submit_write_request(struct raid_bdev_io *raid_io)
{
	struct raid_bdev *raid_bdev = raid_io->raid_bdev;
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	struct spdk_bdev_ext_io_opts io_opts;
	struct raid_base_bdev_info *base_info;
	struct spdk_io_channel *base_ch;
	uint64_t pd_lba, pd_blocks;
	uint8_t idx;
	uint64_t base_bdev_io_not_submitted;
	int ret = 0;

	pd_lba = bdev_io->u.bdev.offset_blocks;
	pd_blocks = bdev_io->u.bdev.num_blocks;

	if (raid_io->base_bdev_io_submitted == 0) {
		raid_io->base_bdev_io_remaining = raid_bdev->num_base_bdevs;
	}

	raid1_init_ext_io_opts(bdev_io, &io_opts);
	for (idx = raid_io->base_bdev_io_submitted; idx < raid_bdev->num_base_bdevs; idx++) {
		base_info = &raid_bdev->base_bdev_info[idx];
		base_ch = raid_io->raid_ch->base_channel[idx];

		if (base_ch == NULL) {
			/* skip a missing base bdev's slot */
			raid_io->base_bdev_io_submitted++;
			raid_bdev_io_complete_part(raid_io, 1, SPDK_BDEV_IO_STATUS_SUCCESS);
			continue;
		}

		ret = raid_bdev_writev_blocks_ext(base_info, base_ch,
						  bdev_io->u.bdev.iovs, bdev_io->u.bdev.iovcnt,
						  pd_lba, pd_blocks, raid1_write_bdev_io_completion,
						  raid_io, &io_opts);
		if (spdk_unlikely(ret != 0)) {
			if (spdk_unlikely(ret == -ENOMEM)) {
				raid_bdev_queue_io_wait(raid_io, spdk_bdev_desc_get_bdev(base_info->desc),
							base_ch, _raid1_submit_rw_request);
				return 0;
			}

			base_bdev_io_not_submitted = raid_bdev->num_base_bdevs -
						     raid_io->base_bdev_io_submitted;
			raid_bdev_io_complete_part(raid_io, base_bdev_io_not_submitted,
						   SPDK_BDEV_IO_STATUS_FAILED);
			return 0;
		}

		raid_io->base_bdev_io_submitted++;
	}

	if (raid_io->base_bdev_io_submitted == 0) {
		ret = -ENODEV;
	}

	return ret;
}

static void
raid1_submit_rw_request(struct raid_bdev_io *raid_io)
{
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	int ret;

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		ret = raid1_submit_read_request(raid_io);
		break;
	case SPDK_BDEV_IO_TYPE_WRITE:
		ret = raid1_submit_write_request(raid_io);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (spdk_unlikely(ret != 0)) {
		raid_bdev_io_complete(raid_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
}

static void
raid1_ioch_destroy(void *io_device, void *ctx_buf)
{
}

static int
raid1_ioch_create(void *io_device, void *ctx_buf)
{
	return 0;
}

static void
raid1_io_device_unregister_done(void *io_device)
{
	struct raid1_info *r1info = io_device;

	raid_bdev_module_stop_done(r1info->raid_bdev);

	free(r1info);
}

static int
raid1_start(struct raid_bdev *raid_bdev)
{
	uint64_t min_blockcnt = UINT64_MAX;
	struct raid_base_bdev_info *base_info;
	struct raid1_info *r1info;
	char name[256];

	r1info = calloc(1, sizeof(*r1info));
	if (!r1info) {
		SPDK_ERRLOG("Failed to allocate RAID1 info device structure\n");
		return -ENOMEM;
	}
	r1info->raid_bdev = raid_bdev;

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		min_blockcnt = spdk_min(min_blockcnt, base_info->data_size);
	}

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		base_info->data_size = min_blockcnt;
	}

	raid_bdev->bdev.blockcnt = min_blockcnt;
	raid_bdev->module_private = r1info;

	snprintf(name, sizeof(name), "raid1_%s", raid_bdev->bdev.name);
	spdk_io_device_register(r1info, raid1_ioch_create, raid1_ioch_destroy,
				sizeof(struct raid1_io_channel) + raid_bdev->num_base_bdevs * sizeof(uint64_t),
				name);

	return 0;
}

static bool
raid1_stop(struct raid_bdev *raid_bdev)
{
	struct raid1_info *r1info = raid_bdev->module_private;

	spdk_io_device_unregister(r1info, raid1_io_device_unregister_done);

	return false;
}

static struct spdk_io_channel *
raid1_get_io_channel(struct raid_bdev *raid_bdev)
{
	struct raid1_info *r1info = raid_bdev->module_private;

	return spdk_get_io_channel(r1info);
}

static struct raid_bdev_module g_raid1_module = {
	.level = RAID1,
	.base_bdevs_min = 2,
	.base_bdevs_constraint = {CONSTRAINT_MIN_BASE_BDEVS_OPERATIONAL, 1},
	.memory_domains_supported = true,
	.start = raid1_start,
	.stop = raid1_stop,
	.submit_rw_request = raid1_submit_rw_request,
	.get_io_channel = raid1_get_io_channel,
};
RAID_MODULE_REGISTER(&g_raid1_module)

SPDK_LOG_REGISTER_COMPONENT(bdev_raid1)
