// SPDX-License-Identifier: GPL-2.0+

#include <linux/virtio_config.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "scatterlist_filler.h"
#include "virtio_media.h"

/* Convert a V4L2 IOCTL into the IOCTL code we can give to the host */
#define VIRTIO_MEDIA_IOCTL_CODE(IOCTL) ((IOCTL >> _IOC_NRSHIFT) & _IOC_NRMASK)

/**
 * Send an ioctl that has no driver payload, but expects a reponse from the host (i.e. an
 * ioctl specified with _IOR).
 *
 * Returns 0 in case of success, or a negative error code.
 */
static int virtio_media_send_r_ioctl(struct v4l2_fh *fh, u32 ioctl_code,
				     const void *ioctl_data,
				     size_t ioctl_data_len)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[3];
	struct scatterlist_filler filler = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_filler_add_ioctl_cmd(&filler, session, ioctl_code);
	if (ret)
		return ret;

	/* Response descriptor */
	ret = scatterlist_filler_add_ioctl_resp(&filler, session);
	if (ret)
		return ret;

	/* Response payload */
	ret = scatterlist_filler_add_data(&filler, (void *)ioctl_data,
					  ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	ret = virtio_media_send_command(
		vv, sgs, 1, 2,
		sizeof(struct virtio_media_resp_ioctl) + ioctl_data_len, NULL);
	if (ret < 0)
		return ret;

	return 0;
}
/**
 * Send an ioctl that does not expect a reply beyond an error status (i.e. an
 * ioctl specified with _IOW) to the host.
 *
 * Returns 0 in case of success, or a negative error code.
 */
static int virtio_media_send_w_ioctl(struct v4l2_fh *fh, u32 ioctl_code,
				     const void *ioctl_data,
				     size_t ioctl_data_len)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[3];
	struct scatterlist_filler filler = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_filler_add_ioctl_cmd(&filler, session, ioctl_code);
	if (ret)
		return ret;

	/* Command payload */
	ret = scatterlist_filler_add_data(&filler, (void *)ioctl_data,
					  ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	/* Response descriptor */
	ret = scatterlist_filler_add_ioctl_resp(&filler, session);
	if (ret)
		return ret;

	ret = virtio_media_send_command(
		vv, sgs, 2, 1, sizeof(struct virtio_media_resp_ioctl), NULL);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * Sends an ioctl that expects a response of exactly the same size as the
 * input (i.e. an ioctl specified with _IOWR) to the host.
 *
 * This corresponds to what most V4L2 ioctls do. For instance VIDIOC_ENUM_FMT
 * takes a partially-initialized struct v4l2_fmtdesc and returns its filled
 * version.
 *
 * Ioctls specified with _IOR can also use this, since the host will simply
 * ignore the extra input data provided.
 *
 * Returns 0 in case of success, or a negative error code.
 */
static int virtio_media_send_wr_ioctl(struct v4l2_fh *fh, u32 ioctl_code,
				      void *ioctl_data, size_t ioctl_data_len,
				      size_t min_resp_payload)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct scatterlist *sgs[4];
	struct scatterlist_filler filler = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	int ret;

	/* Command descriptor */
	ret = scatterlist_filler_add_ioctl_cmd(&filler, session, ioctl_code);
	if (ret)
		return ret;

	/* Command payload */
	ret = scatterlist_filler_add_data(&filler, (void *)ioctl_data,
					  ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to prepare command descriptor chain\n");
		return ret;
	}

	/* Response descriptor */
	ret = scatterlist_filler_add_ioctl_resp(&filler, session);
	if (ret)
		return ret;

	/* Response payload, same as command */
	ret = scatterlist_filler_add_sg(&filler, filler.sgs[1]);
	if (ret)
		return ret;

	ret = virtio_media_send_command(vv, sgs, 2, 2,
					sizeof(struct virtio_media_resp_ioctl) +
						min_resp_payload,
					NULL);
	if (ret < 0)
		return ret;

	ret = scatterlist_filler_retrieve_data(session, filler.sgs[1],
					       ioctl_data, ioctl_data_len);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to retrieve response descriptor chain\n");
		return ret;
	}

	return 0;
}

static int virtio_media_send_buffer_ioctl(struct v4l2_fh *fh, u32 ioctl_code,
					  struct v4l2_buffer *b)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct v4l2_plane *planes_backup = NULL;
	u32 length_backup = 0;
	struct scatterlist *sgs[64];
	size_t num_cmd_sgs;
	struct scatterlist_filler filler = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	size_t resp_len;
	int ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue: %d\n", b->type);
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		planes_backup = b->m.planes;
		length_backup = b->length;
	}

	/* Command descriptor */
	ret = scatterlist_filler_add_ioctl_cmd(&filler, session, ioctl_code);
	if (ret)
		return ret;

	/* Command payload (struct v4l2_buffer) */
	ret = scatterlist_filler_add_buffer(&filler, b, true);
	if (ret < 0)
		return ret;

	num_cmd_sgs = filler.cur_sg;

	/* Response descriptor */
	ret = scatterlist_filler_add_ioctl_resp(&filler, session);
	if (ret)
		return ret;

	/* Response payload (same as input, but no userptr mapping) */
	ret = scatterlist_filler_add_buffer(&filler, b, false);
	if (ret < 0)
		return ret;

	ret = virtio_media_send_command(
		vv, filler.sgs, num_cmd_sgs, filler.cur_sg - num_cmd_sgs,
		sizeof(struct virtio_media_resp_ioctl) + sizeof(*b), &resp_len);

	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		b->m.planes = planes_backup;
		if (b->length > length_backup)
			return -ENOSPC;
	}

	if (ret < 0)
		return ret;

	resp_len -= sizeof(struct virtio_media_resp_ioctl);

	/* Make sure that the reply's length covers our v4l2_buffer */
	if (resp_len < sizeof(*b))
		return -EINVAL;

	ret = scatterlist_filler_retrieve_buffer(
		session, &sgs[1], num_cmd_sgs - 1, b, length_backup);
	if (ret) {
		v4l2_err(&vv->v4l2_dev,
			 "failed to retrieve response descriptor chain\n");
		return ret;
	}

	/* TODO ideally we should not be doing this twice, but the scatterlist may screw us up here? */
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		b->m.planes = planes_backup;
		if (b->length > length_backup)
			return -ENOSPC;
	}

	return 0;
}

/**
 * Queues an ioctl that sends a v4l2_ext_controls to the host and receives an updated version.
 *
 * v4l2_ext_controls has a pointer to an array of v4l2_ext_control, and also
 * potentially pointers to user-space memory that we need to map properly,
 * hence the dedicated function.
 *
 */
static int virtio_media_send_ext_controls_ioctl(struct v4l2_fh *fh,
						u32 ioctl_code,
						struct v4l2_ext_controls *ctrls)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	size_t num_cmd_sgs;
	struct v4l2_ext_control *controls_backup = ctrls->controls;
	const u32 num_ctrls = ctrls->count;
	struct scatterlist *sgs[64];
	struct scatterlist_filler filler = {
		.descs = session->command_sgs.sgl,
		.num_descs = DESC_CHAIN_MAX_LEN,
		.cur_desc = 0,
		.shadow_buffer = session->shadow_buf,
		.shadow_buffer_size = VIRTIO_BUF_SIZE,
		.shadow_buffer_pos = 0,
		.sgs = sgs,
		.num_sgs = ARRAY_SIZE(sgs),
		.cur_sg = 0,
	};
	size_t resp_len = 0;
	int ret;

	/* Command descriptor */
	ret = scatterlist_filler_add_ioctl_cmd(&filler, session, ioctl_code);
	if (ret)
		return ret;

	/* v4l2_controls and its pointees */
	ret = scatterlist_filler_add_ext_ctrls(&filler, ctrls, true);
	if (ret)
		return ret;

	num_cmd_sgs = filler.cur_sg;

	/* Response descriptor */
	ret = scatterlist_filler_add_ioctl_resp(&filler, session);
	if (ret)
		return ret;

	/*
	 * Response payload (same as input but without userptrs)
	 */
	ret = scatterlist_filler_add_ext_ctrls(&filler, ctrls, false);
	if (ret)
		return ret;

	ret = virtio_media_send_command(
		vv, filler.sgs, num_cmd_sgs, filler.cur_sg - num_cmd_sgs,
		sizeof(struct virtio_media_resp_ioctl) + sizeof(*ctrls),
		&resp_len);

	/* Just in case the host touched these. */
	ctrls->controls = controls_backup;
	if (ctrls->count != num_ctrls) {
		v4l2_err(
			&vv->v4l2_dev,
			"device returned a number of extended controls different than submitted\n");
	}
	if (ctrls->count > num_ctrls)
		return -ENOSPC;

	/* Event if we have received an error, we may need to read our payload back */
	if (ret < 0 && resp_len >= sizeof(struct virtio_media_resp_ioctl) +
					   sizeof(*ctrls)) {
		/* Deliberately ignore the error here as we want to return the previous one */
		scatterlist_filler_retrieve_ext_ctrls(session, &sgs[1],
						      num_cmd_sgs - 1, ctrls);
		return ret;
	}

	resp_len -= sizeof(struct virtio_media_resp_ioctl);

	/* Make sure that the reply's length covers our v4l2_ext_controls */
	if (resp_len < sizeof(*ctrls))
		return -EINVAL;

	ret = scatterlist_filler_retrieve_ext_ctrls(session, &sgs[1],
						    num_cmd_sgs - 1, ctrls);
	if (ret)
		return ret;

	return 0;
}

/**
 * Helper function to clear the list of buffers waiting to be dequeued on a
 * queue that has just been streamed off.
 */
static void virtio_media_clear_queue(struct virtio_media *vv,
				     struct virtio_media_session *session,
				     struct virtio_media_queue_state *queue)
{
	struct list_head *p, *n;

	mutex_lock(&session->dqbufs_lock);

	list_for_each_safe(p, n, &queue->pending_dqbufs) {
		struct virtio_media_buffer *dqbuf =
			list_entry(p, struct virtio_media_buffer, list);

		list_del(&dqbuf->list);
	}

	mutex_unlock(&session->dqbufs_lock);

	queue->queued_bufs = 0;
	queue->streaming = false;
	queue->is_capture_last = false;
}

/*
 * Macros suitable for defining ioctls with a constant size payload.
 */

#define SIMPLE_WR_IOCTL(name, ioctl, type)                           \
	static int virtio_media_##name(struct file *file, void *fh,  \
				       type *payload)                \
	{                                                            \
		return virtio_media_send_wr_ioctl(                   \
			fh, VIRTIO_MEDIA_IOCTL_CODE(ioctl), payload, \
			sizeof(*payload), sizeof(*payload));         \
	}
#define SIMPLE_R_IOCTL(name, ioctl, type)                            \
	static int virtio_media_##name(struct file *file, void *fh,  \
				       type *payload)                \
	{                                                            \
		return virtio_media_send_r_ioctl(                    \
			fh, VIRTIO_MEDIA_IOCTL_CODE(ioctl), payload, \
			sizeof(*payload));                           \
	}
#define SIMPLE_W_IOCTL(name, ioctl, type)                            \
	static int virtio_media_##name(struct file *file, void *fh,  \
				       type *payload)                \
	{                                                            \
		return virtio_media_send_w_ioctl(                    \
			fh, VIRTIO_MEDIA_IOCTL_CODE(ioctl), payload, \
			sizeof(*payload));                           \
	}

/*
 * V4L2 ioctl handlers.
 *
 * Most of these functions just forward the ioctl to the host, for these we can
 * use one of the SIMPLE_*_IOCTL macros. Exceptions that have their own
 * standalone function follow.
 */

SIMPLE_WR_IOCTL(enum_fmt, VIDIOC_ENUM_FMT, struct v4l2_fmtdesc)
SIMPLE_WR_IOCTL(g_fmt, VIDIOC_G_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(s_fmt, VIDIOC_S_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(try_fmt, VIDIOC_TRY_FMT, struct v4l2_format)
SIMPLE_WR_IOCTL(enum_framesizes, VIDIOC_ENUM_FRAMESIZES,
		struct v4l2_frmsizeenum)
SIMPLE_WR_IOCTL(enum_frameintervals, VIDIOC_ENUM_FRAMEINTERVALS,
		struct v4l2_frmivalenum)
SIMPLE_WR_IOCTL(queryctrl, VIDIOC_QUERYCTRL, struct v4l2_queryctrl)
SIMPLE_WR_IOCTL(g_ctrl, VIDIOC_G_CTRL, struct v4l2_control)
SIMPLE_WR_IOCTL(s_ctrl, VIDIOC_S_CTRL, struct v4l2_control)
SIMPLE_WR_IOCTL(query_ext_ctrl, VIDIOC_QUERY_EXT_CTRL,
		struct v4l2_query_ext_ctrl)
SIMPLE_WR_IOCTL(s_dv_timings, VIDIOC_S_DV_TIMINGS, struct v4l2_dv_timings)
SIMPLE_WR_IOCTL(g_dv_timings, VIDIOC_G_DV_TIMINGS, struct v4l2_dv_timings)
SIMPLE_R_IOCTL(query_dv_timings, VIDIOC_QUERY_DV_TIMINGS,
	       struct v4l2_dv_timings)
SIMPLE_WR_IOCTL(enum_dv_timings, VIDIOC_ENUM_DV_TIMINGS,
		struct v4l2_enum_dv_timings)
SIMPLE_WR_IOCTL(dv_timings_cap, VIDIOC_DV_TIMINGS_CAP,
		struct v4l2_dv_timings_cap)
SIMPLE_WR_IOCTL(enuminput, VIDIOC_ENUMINPUT, struct v4l2_input)
SIMPLE_WR_IOCTL(querymenu, VIDIOC_QUERYMENU, struct v4l2_querymenu)
SIMPLE_WR_IOCTL(enumoutput, VIDIOC_ENUMOUTPUT, struct v4l2_output)
SIMPLE_WR_IOCTL(enumaudio, VIDIOC_ENUMAUDIO, struct v4l2_audio)
SIMPLE_R_IOCTL(g_audio, VIDIOC_G_AUDIO, struct v4l2_audio)
SIMPLE_W_IOCTL(s_audio, VIDIOC_S_AUDIO, const struct v4l2_audio)
SIMPLE_WR_IOCTL(enumaudout, VIDIOC_ENUMAUDOUT, struct v4l2_audioout)
SIMPLE_R_IOCTL(g_audout, VIDIOC_G_AUDOUT, struct v4l2_audioout)
SIMPLE_W_IOCTL(s_audout, VIDIOC_S_AUDOUT, const struct v4l2_audioout)
SIMPLE_WR_IOCTL(g_modulator, VIDIOC_G_MODULATOR, struct v4l2_modulator)
SIMPLE_W_IOCTL(s_modulator, VIDIOC_S_MODULATOR, const struct v4l2_modulator)
SIMPLE_WR_IOCTL(g_selection, VIDIOC_G_SELECTION, struct v4l2_selection)
SIMPLE_WR_IOCTL(s_selection, VIDIOC_S_SELECTION, struct v4l2_selection)
SIMPLE_R_IOCTL(g_enc_index, VIDIOC_G_ENC_INDEX, struct v4l2_enc_idx)
SIMPLE_WR_IOCTL(encoder_cmd, VIDIOC_ENCODER_CMD, struct v4l2_encoder_cmd)
SIMPLE_WR_IOCTL(try_encoder_cmd, VIDIOC_TRY_ENCODER_CMD,
		struct v4l2_encoder_cmd)
SIMPLE_WR_IOCTL(try_decoder_cmd, VIDIOC_TRY_DECODER_CMD,
		struct v4l2_decoder_cmd)
SIMPLE_WR_IOCTL(g_parm, VIDIOC_G_PARM, struct v4l2_streamparm)
SIMPLE_WR_IOCTL(s_parm, VIDIOC_S_PARM, struct v4l2_streamparm)
SIMPLE_R_IOCTL(g_std, VIDIOC_G_STD, v4l2_std_id)
SIMPLE_R_IOCTL(querystd, VIDIOC_QUERYSTD, v4l2_std_id)
SIMPLE_WR_IOCTL(enumstd, VIDIOC_ENUMSTD, struct v4l2_standard)
SIMPLE_WR_IOCTL(g_tuner, VIDIOC_G_TUNER, struct v4l2_tuner)
SIMPLE_W_IOCTL(s_tuner, VIDIOC_S_TUNER, const struct v4l2_tuner)
SIMPLE_WR_IOCTL(g_frequency, VIDIOC_G_FREQUENCY, struct v4l2_frequency)
SIMPLE_W_IOCTL(s_frequency, VIDIOC_S_FREQUENCY, const struct v4l2_frequency)
SIMPLE_WR_IOCTL(enum_freq_bands, VIDIOC_ENUM_FREQ_BANDS,
		struct v4l2_frequency_band)
SIMPLE_WR_IOCTL(g_sliced_vbi_cap, VIDIOC_G_SLICED_VBI_CAP,
		struct v4l2_sliced_vbi_cap)
SIMPLE_W_IOCTL(s_hw_freq_seek, VIDIOC_S_HW_FREQ_SEEK,
	       const struct v4l2_hw_freq_seek)

/*
 * QUERYCAP is handled by reading the configuration area.
 *
 */

static int virtio_media_querycap(struct file *file, void *fh,
				 struct v4l2_capability *cap)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);

	/* TODO add proper number? */
	strncpy(cap->bus_info, "platform:virtio-media0", sizeof(cap->bus_info));

	if (!driver_name) {
		strncpy(cap->driver, VIRTIO_MEDIA_DEFAULT_DRIVER_NAME,
			sizeof(cap->driver));
	} else {
		strncpy(cap->driver, driver_name, sizeof(cap->driver));
	}
	virtio_cread_bytes(vv->virtio_dev, 8, cap->card, sizeof(cap->card));

	cap->capabilities = video_dev->device_caps | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = video_dev->device_caps;

	return 0;
}

/*
 * Extended control ioctls are handled mostly identically.
 */

static int virtio_media_g_ext_ctrls(struct file *file, void *fh,
				    struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_G_EXT_CTRLS), ctrls);
}

static int virtio_media_s_ext_ctrls(struct file *file, void *fh,
				    struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_S_EXT_CTRLS), ctrls);
}

static int virtio_media_try_ext_ctrls(struct file *file, void *fh,
				      struct v4l2_ext_controls *ctrls)
{
	return virtio_media_send_ext_controls_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_TRY_EXT_CTRLS), ctrls);
}

/*
 * Subscribe/unsubscribe from an event.
 */

static int
virtio_media_subscribe_event(struct v4l2_fh *fh,
			     const struct v4l2_event_subscription *sub)
{
	struct video_device *video_dev = fh->vdev;
	struct virtio_media *vv = to_virtio_media(video_dev);
	int ret;

	/* First subscribe to the event in the guest. */
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(fh, sub);
		break;
	default:
		ret = v4l2_event_subscribe(fh, sub, 1, NULL);
		break;
	}
	if (ret)
		return ret;

	/* Then ask the host to signal us these events. */
	ret = virtio_media_send_w_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_SUBSCRIBE_EVENT), sub,
		sizeof(*sub));
	if (ret < 0) {
		v4l2_event_unsubscribe(fh, sub);
		return ret;
	}

	/*
	 * Subscribing to an event may result in that event being signaled
	 * immediately. Process all pending events to make sure we don't miss it.
	 */
	if (sub->flags & V4L2_EVENT_SUB_FL_SEND_INITIAL) {
		virtio_media_process_events(vv);
	}

	return 0;
}

static int
virtio_media_unsubscribe_event(struct v4l2_fh *fh,
			       const struct v4l2_event_subscription *sub)
{
	int ret;

	ret = virtio_media_send_w_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_UNSUBSCRIBE_EVENT), sub,
		sizeof(*sub));
	if (ret < 0)
		return ret;

	ret = v4l2_event_unsubscribe(fh, sub);
	if (ret)
		return ret;

	return 0;
}

/*
 * Streamon/off affect the local queue state.
 */

static int virtio_media_streamon(struct file *file, void *fh,
				 enum v4l2_buf_type i)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	if (i > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue: %d\n", i);
		return -EINVAL;
	}

	ret = virtio_media_send_w_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_STREAMON), &i, sizeof(i));
	if (ret < 0)
		return ret;

	session->queues[i].streaming = true;

	return 0;
}

static int virtio_media_streamoff(struct file *file, void *fh,
				  enum v4l2_buf_type i)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	if (i > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue: %d\n", i);
		return -EINVAL;
	}

	ret = virtio_media_send_w_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_STREAMOFF), &i, sizeof(i));
	if (ret < 0)
		return ret;

	virtio_media_clear_queue(vv, session, &session->queues[i]);

	return 0;
}

/*
 * Buffer creation/queuing functions deal with the local driver state.
 */

static int virtio_media_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *b)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	int ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue: %d\n", b->type);
		return -EINVAL;
	}

	ret = virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_REQBUFS), b, sizeof(*b),
		sizeof(*b));
	if (ret)
		return ret;

	queue = &session->queues[b->type];

	vfree(queue->buffers);
	queue->buffers = NULL;

	/* REQBUFS(0) is an implicit STREAMOFF. */
	if (b->count == 0) {
		virtio_media_clear_queue(vv, session, queue);
	} else {
		queue->buffers =
			vzalloc(sizeof(struct virtio_media_buffer) * b->count);
		if (!queue->buffers) {
			return -ENOMEM;
		}
	}

	queue->allocated_bufs = b->count;

	/*
	 * If a multiplanar queue is successfully used here, this means
	 * we are using the multiplanar interface.
	 */
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		session->uses_mplane = true;
	}

	/* TODO remove once we support DMABUFs */
	b->capabilities &= ~V4L2_BUF_CAP_SUPPORTS_DMABUF;

	return 0;
}

static int virtio_media_querybuf(struct file *file, void *fh,
				 struct v4l2_buffer *b)
{
	int ret;

	ret = virtio_media_send_buffer_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_QUERYBUF), b);
	if (ret)
		return ret;

	return 0;
}

static int virtio_media_create_bufs(struct file *file, void *fh,
				    struct v4l2_create_buffers *b)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffers;
	u32 type = b->format.type;
	int ret;

	if (type > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue: %d\n", type);
		return -EINVAL;
	}

	queue = &session->queues[type];

	ret = virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_CREATE_BUFS), b, sizeof(*b),
		sizeof(*b));
	if (ret)
		return ret;

	/* If count is zero, we were just checking for format. */
	if (b->count == 0)
		return 0;

	buffers = queue->buffers;

	queue->buffers = vzalloc(sizeof(struct virtio_media_buffer) *
				 (b->index + b->count));
	if (!queue->buffers) {
		queue->buffers = buffers;
		return -ENOMEM;
	}

	memcpy(queue->buffers, buffers,
	       sizeof(*buffers) * queue->allocated_bufs);
	vfree(buffers);

	queue->allocated_bufs = b->index + b->count;

	return 0;
}

static int virtio_media_prepare_buf(struct file *file, void *fh,
				    struct v4l2_buffer *b)
{
	int ret;

	ret = virtio_media_send_buffer_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_PREPARE_BUF), b);
	if (ret)
		return ret;

	/* TODO should we store some information? */

	return 0;
}

static int virtio_media_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct virtio_media_session *session = fh_to_session(fh);
	struct virtio_media_queue_state *queue;
	struct virtio_media_buffer *buffer;
	int ret;

	ret = virtio_media_send_buffer_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_QBUF), b);
	if (ret)
		return ret;

	/*
	 * Store the buffer information so we can retrieve it again when DQBUF
	 * occurs.
	 */
	queue = &session->queues[b->type];
	queue->queued_bufs += 1;
	buffer = &queue->buffers[b->index];
	memcpy(&buffer->buffer, b, sizeof(*b));
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type) && b->length > 0) {
		memcpy(buffer->planes, b->m.planes,
		       sizeof(struct v4l2_plane) * b->length);
	}

	return 0;
}

static int virtio_media_dqbuf(struct file *file, void *fh,
			      struct v4l2_buffer *b)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_media *vv = to_virtio_media(video_dev);
	struct virtio_media_session *session =
		fh_to_session(file->private_data);
	struct virtio_media_buffer *dqbuf;
	struct virtio_media_queue_state *queue;
	struct list_head *buffer_queue;
	struct v4l2_plane *planes_backup = NULL;
	const bool is_multiplanar = V4L2_TYPE_IS_MULTIPLANAR(b->type);
	int ret;

	if (b->type > VIRTIO_MEDIA_LAST_QUEUE) {
		v4l2_err(&vv->v4l2_dev, "unsupported queue for dqbuf: %d\n",
			 b->type);
		return -EINVAL;
	}

	queue = &session->queues[b->type];

	/*
	 * If a buffer with the LAST flag has been returned, subsequent calls to DQBUF
	 * must return -EPIPE until the queue is cleared.
	 */
	if (queue->is_capture_last)
		return -EPIPE;

	buffer_queue = &queue->pending_dqbufs;

	/* Only block for a buffer if the file has been opened with O_NONBLOCK. */
	if (session->nonblocking_dequeue) {
		if (list_empty(buffer_queue))
			return -EAGAIN;
	} else if (queue->allocated_bufs == 0) {
		return -EINVAL;
	} else if (!queue->streaming) {
		return -EINVAL;
	} else {
		ret = wait_event_interruptible(session->dqbufs_wait,
					       !list_empty(buffer_queue));
		if (ret)
			return -EINTR;
	}

	mutex_lock(&session->dqbufs_lock);
	dqbuf = list_first_entry(buffer_queue, struct virtio_media_buffer,
				 list);
	list_del(&dqbuf->list);
	mutex_unlock(&session->dqbufs_lock);

	if (is_multiplanar) {
		size_t nb_planes = min(b->length, (u32)VIDEO_MAX_PLANES);
		memcpy(b->m.planes, dqbuf->planes,
		       nb_planes * sizeof(struct v4l2_plane));
		planes_backup = b->m.planes;
	}

	memcpy(b, &dqbuf->buffer, sizeof(*b));

	if (is_multiplanar) {
		b->m.planes = planes_backup;
	}

	if (V4L2_TYPE_IS_CAPTURE(b->type) && b->flags & V4L2_BUF_FLAG_LAST) {
		queue->is_capture_last = true;
	}

	return 0;
}

/*
 * s/g_input/output work with an unsigned int - recast this to a u32 so the
 * size is unambiguous.
 */

static int virtio_media_g_input(struct file *file, void *fh, unsigned int *i)
{
	u32 input;
	int ret;

	ret = virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_G_INPUT), &input,
		sizeof(input), sizeof(input));
	if (ret)
		return ret;

	*i = input;

	return 0;
}

static int virtio_media_s_input(struct file *file, void *fh, unsigned int i)
{
	u32 input = i;

	return virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_S_INPUT), &input,
		sizeof(input), sizeof(input));
}

static int virtio_media_g_output(struct file *file, void *fh, unsigned int *o)
{
	u32 output;
	int ret;

	ret = virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_G_OUTPUT), &output,
		sizeof(output), sizeof(output));
	if (ret)
		return ret;

	*o = output;

	return 0;
}

static int virtio_media_s_output(struct file *file, void *fh, unsigned int o)
{
	u32 output = o;

	return virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_S_OUTPUT), &output,
		sizeof(output), sizeof(output));
}

/*
 * decoder_cmd can affect the state of the CAPTURE queue.
 */

static int virtio_media_decoder_cmd(struct file *file, void *fh,
				    struct v4l2_decoder_cmd *cmd)
{
	struct virtio_media_session *session = fh_to_session(fh);
	int ret;

	ret = virtio_media_send_wr_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_DECODER_CMD), cmd,
		sizeof(*cmd), sizeof(*cmd));
	if (ret)
		return ret;

	/* A START command makes the CAPTURE queue able to dequeue again. */
	if (cmd->cmd == V4L2_DEC_CMD_START) {
		session->queues[V4L2_BUF_TYPE_VIDEO_CAPTURE].is_capture_last =
			false;
		session->queues[V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]
			.is_capture_last = false;
	}

	return 0;
}

/*
 * s_std doesn't work with a pointer, so we cannot use SIMPLE_W_IOCTL.
 */

static int virtio_media_s_std(struct file *file, void *fh, v4l2_std_id s)
{
	int ret;

	ret = virtio_media_send_w_ioctl(
		fh, VIRTIO_MEDIA_IOCTL_CODE(VIDIOC_S_STD), &s, sizeof(s));
	if (ret)
		return ret;

	return 0;
}

const struct v4l2_ioctl_ops virtio_media_ioctl_ops = {
	/* VIDIOC_QUERYCAP handler */
	.vidioc_querycap = virtio_media_querycap,

	/* VIDIOC_ENUM_FMT handlers */
	.vidioc_enum_fmt_vid_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_vid_overlay = virtio_media_enum_fmt,
	.vidioc_enum_fmt_vid_out = virtio_media_enum_fmt,
	.vidioc_enum_fmt_sdr_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_sdr_out = virtio_media_enum_fmt,
	.vidioc_enum_fmt_meta_cap = virtio_media_enum_fmt,
	.vidioc_enum_fmt_meta_out = virtio_media_enum_fmt,

	/* VIDIOC_G_FMT handlers */
	.vidioc_g_fmt_vid_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_overlay = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out_overlay = virtio_media_g_fmt,
	.vidioc_g_fmt_vbi_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_vbi_out = virtio_media_g_fmt,
	.vidioc_g_fmt_sliced_vbi_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_sliced_vbi_out = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_cap_mplane = virtio_media_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = virtio_media_g_fmt,
	.vidioc_g_fmt_sdr_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_sdr_out = virtio_media_g_fmt,
	.vidioc_g_fmt_meta_cap = virtio_media_g_fmt,
	.vidioc_g_fmt_meta_out = virtio_media_g_fmt,

	/* VIDIOC_S_FMT handlers */
	.vidioc_s_fmt_vid_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_overlay = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out_overlay = virtio_media_s_fmt,
	.vidioc_s_fmt_vbi_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_vbi_out = virtio_media_s_fmt,
	.vidioc_s_fmt_sliced_vbi_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_sliced_vbi_out = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_cap_mplane = virtio_media_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = virtio_media_s_fmt,
	.vidioc_s_fmt_sdr_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_sdr_out = virtio_media_s_fmt,
	.vidioc_s_fmt_meta_cap = virtio_media_s_fmt,
	.vidioc_s_fmt_meta_out = virtio_media_s_fmt,

	/* VIDIOC_TRY_FMT handlers */
	.vidioc_try_fmt_vid_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_overlay = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out_overlay = virtio_media_try_fmt,
	.vidioc_try_fmt_vbi_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_vbi_out = virtio_media_try_fmt,
	.vidioc_try_fmt_sliced_vbi_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_sliced_vbi_out = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_cap_mplane = virtio_media_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = virtio_media_try_fmt,
	.vidioc_try_fmt_sdr_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_sdr_out = virtio_media_try_fmt,
	.vidioc_try_fmt_meta_cap = virtio_media_try_fmt,
	.vidioc_try_fmt_meta_out = virtio_media_try_fmt,

	/* Buffer handlers */
	.vidioc_reqbufs = virtio_media_reqbufs,
	.vidioc_querybuf = virtio_media_querybuf,
	.vidioc_qbuf = virtio_media_qbuf,
	.vidioc_expbuf = NULL,
	.vidioc_dqbuf = virtio_media_dqbuf,
	.vidioc_create_bufs = virtio_media_create_bufs,
	.vidioc_prepare_buf = virtio_media_prepare_buf,
	/* Overlay interface not supported yet */
	.vidioc_overlay = NULL,
	/* Overlay interface not supported yet */
	.vidioc_g_fbuf = NULL,
	/* Overlay interface not supported yet */
	.vidioc_s_fbuf = NULL,

	/* Stream on/off */
	.vidioc_streamon = virtio_media_streamon,
	.vidioc_streamoff = virtio_media_streamoff,

	/* Standard handling */
	.vidioc_g_std = virtio_media_g_std,
	.vidioc_s_std = virtio_media_s_std,
	.vidioc_querystd = virtio_media_querystd,

	/* Input handling */
	.vidioc_enum_input = virtio_media_enuminput,
	.vidioc_g_input = virtio_media_g_input,
	.vidioc_s_input = virtio_media_s_input,

	/* Output handling */
	.vidioc_enum_output = virtio_media_enumoutput,
	.vidioc_g_output = virtio_media_g_output,
	.vidioc_s_output = virtio_media_s_output,

	/* Control handling */
	.vidioc_queryctrl = virtio_media_queryctrl,
	.vidioc_query_ext_ctrl = virtio_media_query_ext_ctrl,
	.vidioc_g_ctrl = virtio_media_g_ctrl,
	.vidioc_s_ctrl = virtio_media_s_ctrl,
	.vidioc_g_ext_ctrls = virtio_media_g_ext_ctrls,
	.vidioc_s_ext_ctrls = virtio_media_s_ext_ctrls,
	.vidioc_try_ext_ctrls = virtio_media_try_ext_ctrls,
	.vidioc_querymenu = virtio_media_querymenu,

	/* Audio ioctls */
	.vidioc_enumaudio = virtio_media_enumaudio,
	.vidioc_g_audio = virtio_media_g_audio,
	.vidioc_s_audio = virtio_media_s_audio,

	/* Audio out ioctls */
	.vidioc_enumaudout = virtio_media_enumaudout,
	.vidioc_g_audout = virtio_media_g_audout,
	.vidioc_s_audout = virtio_media_s_audout,
	.vidioc_g_modulator = virtio_media_g_modulator,
	.vidioc_s_modulator = virtio_media_s_modulator,

	/* Crop ioctls */
	/* Not directly an ioctl (part of VIDIOC_CROPCAP), so no need to implement */
	.vidioc_g_pixelaspect = NULL,
	.vidioc_g_selection = virtio_media_g_selection,
	.vidioc_s_selection = virtio_media_s_selection,

	/* Compression ioctls */
	/* Deprecated in V4L2. */
	.vidioc_g_jpegcomp = NULL,
	/* Deprecated in V4L2. */
	.vidioc_s_jpegcomp = NULL,
	.vidioc_g_enc_index = virtio_media_g_enc_index,
	.vidioc_encoder_cmd = virtio_media_encoder_cmd,
	.vidioc_try_encoder_cmd = virtio_media_try_encoder_cmd,
	.vidioc_decoder_cmd = virtio_media_decoder_cmd,
	.vidioc_try_decoder_cmd = virtio_media_try_decoder_cmd,

	/* Stream type-dependent parameter ioctls */
	.vidioc_g_parm = virtio_media_g_parm,
	.vidioc_s_parm = virtio_media_s_parm,

	/* Tuner ioctls */
	.vidioc_g_tuner = virtio_media_g_tuner,
	.vidioc_s_tuner = virtio_media_s_tuner,
	.vidioc_g_frequency = virtio_media_g_frequency,
	.vidioc_s_frequency = virtio_media_s_frequency,
	.vidioc_enum_freq_bands = virtio_media_enum_freq_bands,

	/* Sliced VBI cap */
	.vidioc_g_sliced_vbi_cap = virtio_media_g_sliced_vbi_cap,

	/* Log status ioctl */
	/* Guest-only operation */
	.vidioc_log_status = NULL,

	.vidioc_s_hw_freq_seek = virtio_media_s_hw_freq_seek,

	.vidioc_enum_framesizes = virtio_media_enum_framesizes,
	.vidioc_enum_frameintervals = virtio_media_enum_frameintervals,

	/* DV Timings IOCTLs */
	.vidioc_s_dv_timings = virtio_media_s_dv_timings,
	.vidioc_g_dv_timings = virtio_media_g_dv_timings,
	.vidioc_query_dv_timings = virtio_media_query_dv_timings,
	.vidioc_enum_dv_timings = virtio_media_enum_dv_timings,
	.vidioc_dv_timings_cap = virtio_media_dv_timings_cap,
	.vidioc_g_edid = NULL,
	.vidioc_s_edid = NULL,

	.vidioc_subscribe_event = virtio_media_subscribe_event,
	.vidioc_unsubscribe_event = virtio_media_unsubscribe_event,

	/* For other private ioctls */
	.vidioc_default = NULL,
};

long virtio_media_device_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct video_device *vfd = video_devdata(file);
	struct v4l2_fh *vfh = NULL;
	struct v4l2_standard standard;
	v4l2_std_id std_id = 0;
	int ret;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags))
		vfh = file->private_data;

	/*
	 * We need to handle a few ioctls manually because their result rely on
	 * vfd->tvnorms, which is normally updated by the driver as S_INPUT is
	 * called. Since we want to just pass these ioctls through, we have to hijack
	 * them from here.
	 */
	switch (cmd) {
	case VIDIOC_S_STD:
		ret = copy_from_user(&std_id, (void __user *)arg,
				     sizeof(std_id));
		if (ret)
			return -EINVAL;
		return virtio_media_s_std(file, vfh, std_id);
	case VIDIOC_ENUMSTD: {
		ret = copy_from_user(&standard, (void __user *)arg,
				     sizeof(standard));
		if (ret)
			return -EINVAL;
		ret = virtio_media_enumstd(file, vfh, &standard);
		if (ret)
			return ret;
		ret = copy_to_user((void __user *)arg, &standard,
				   sizeof(standard));
		if (ret)
			return -EINVAL;
		return 0;
	}
	case VIDIOC_QUERYSTD: {
		ret = virtio_media_querystd(file, vfh, &std_id);
		if (ret)
			return ret;
		ret = copy_to_user((void __user *)arg, &std_id, sizeof(std_id));
		if (ret)
			return -EINVAL;
		return 0;
	}
	default:
		return video_ioctl2(file, cmd, arg);
	}
}
