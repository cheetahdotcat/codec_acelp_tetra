/*
 * Asterisk -- An open source telephony toolkit.
 *
 * codec_acelp_tetra.c
 *
 * Translate between signed linear audio and TETRA ACELP/8000 using
 * in-process ETSI reference codec functions.
 */

#ifndef AST_MODULE
#define AST_MODULE "codec_acelp_tetra"
#endif

#ifndef AST_MODULE_SELF_SYM
#define AST_MODULE_SELF_SYM __internal_codec_acelp_tetra_self
#endif

#include "asterisk.h"

#if defined(ASTERISK_REGISTER_FILE)
ASTERISK_REGISTER_FILE()
#elif defined(ASTERISK_FILE_VERSION)
ASTERISK_FILE_VERSION(__FILE__, "")
#endif

#include "asterisk/astobj2.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"
#include "asterisk/frame.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/slin.h"
#include "asterisk/translate.h"
#include "asterisk/utils.h"

#include <stdint.h>
#include <string.h>

/*
 * source.h is provided by the patched ETSI TETRA codec source tree.
 * Build with TETRA_CODEC_SRC pointing to the c-code directory.
 */
#include "source.h"
#include "acelp_state_bridge.h"

#define ACELP_SAMPLE_RATE 8000
#define ACELP_FRAME_MS 30
#define ACELP_FRAME_SAMPLES 240
#define ACELP_FRAME_BITS 137
#define ACELP_FRAME_BYTES ((ACELP_FRAME_BITS + 7) / 8) /* 18 */
#define ACELP_SERIAL_WORDS (ACELP_FRAME_BITS + 1)      /* BFI + bits */
#define ACELP_PRM_WORDS 24                              /* BFI + 23 params */

/* 1 second of 8kHz linear PCM */
#define ACELP_BUFFER_SAMPLES 8000

struct acelp_encoder_state {
	acelp_scod_state_t scod;
	acelp_preproc_state_t preproc;
	acelp_postproc_state_t postproc;
	acelp_tetraop_state_t tetraop;
	int initialized;
};

struct acelp_decoder_state {
	acelp_sdec_state_t sdec;
	acelp_postproc_state_t postproc;
	acelp_tetraop_state_t tetraop;
	int initialized;
};

struct acelp_translator_pvt {
	int16_t slin_buf[ACELP_BUFFER_SAMPLES];
	struct acelp_encoder_state encoder;
	struct acelp_decoder_state decoder;
};

static struct ast_format *ast_format_acelp;
static uint8_t ex_acelp[ACELP_FRAME_BYTES] = { 0 };

/* ETSI reference code uses globals/static internals; guard global swaps. */
AST_MUTEX_DEFINE_STATIC(acelp_codec_lock);

/* Defined by scod_tet.c */
extern Word16 *new_speech;

static void acelp_postproc_state_init(acelp_postproc_state_t *state)
{
	int i;

	memset(state, 0, sizeof(*state));
	state->old_a[0] = 4096;
	for (i = 1; i < ACELP_POSTPROC_A_LEN; ++i) {
		state->old_a[i] = 0;
	}
}

static void payload_to_serial(const uint8_t *payload, Word16 *serial)
{
	size_t bit;

	serial[0] = 0; /* BFI */
        for (bit = 0; bit < ACELP_FRAME_BITS; ++bit) {
                size_t byte_off = bit / 8;
		unsigned int bit_off = 7 - (bit % 8);

		serial[bit + 1] = (payload[byte_off] >> bit_off) & 0x01;
	}
}

static void serial_to_payload(const Word16 *serial, uint8_t *payload)
{
	size_t bit;

	memset(payload, 0, ACELP_FRAME_BYTES);
	for (bit = 0; bit < ACELP_FRAME_BITS; ++bit) {
		size_t byte_off = bit / 8;
		unsigned int bit_off = 7 - (bit % 8);

		if (serial[bit + 1] & 0x01) {
			payload[byte_off] |= (1u << bit_off);
		}
	}
}

static int encode_linear_frames(struct acelp_encoder_state *state, const int16_t *pcm_in,
	size_t frames, uint8_t *payload_out)
{
	size_t i;
	Word16 ana[23];
	Word16 syn[ACELP_FRAME_SAMPLES];
	Word16 serial[ACELP_SERIAL_WORDS];

	ast_mutex_lock(&acelp_codec_lock);

	if (!state->initialized) {
		Init_Pre_Process();
		Init_Coder_Tetra();
		acelp_preproc_state_get(&state->preproc);
		acelp_scod_state_get(&state->scod);
		acelp_postproc_state_init(&state->postproc);
		acelp_tetraop_state_get(&state->tetraop);
		state->initialized = 1;
	}

	acelp_tetraop_state_set(&state->tetraop);
	acelp_preproc_state_set(&state->preproc);
	acelp_postproc_state_set(&state->postproc);
	acelp_scod_state_set(&state->scod);

	for (i = 0; i < frames; ++i) {
		const int16_t *src = pcm_in + (i * ACELP_FRAME_SAMPLES);
		uint8_t *dst_payload = payload_out + (i * ACELP_FRAME_BYTES);

		/* Coder expects input in its global new_speech buffer. */
		memcpy(new_speech, src, ACELP_FRAME_SAMPLES * sizeof(Word16));
		Pre_Process(new_speech, ACELP_FRAME_SAMPLES);
		Coder_Tetra(ana, syn);
		Post_Process(syn, ACELP_FRAME_SAMPLES);
		Prm2bits_Tetra(ana, serial);

		serial_to_payload(serial, dst_payload);
	}

	acelp_scod_state_get(&state->scod);
	acelp_postproc_state_get(&state->postproc);
	acelp_preproc_state_get(&state->preproc);
	acelp_tetraop_state_get(&state->tetraop);

	ast_mutex_unlock(&acelp_codec_lock);
	return 0;
}

static int decode_payload_frames(struct acelp_decoder_state *state, const uint8_t *payload_in,
	size_t frames, int16_t *pcm_out)
{
	size_t i;
	Word16 serial[ACELP_SERIAL_WORDS];
	Word16 parm[ACELP_PRM_WORDS];
	Word16 synth[ACELP_FRAME_SAMPLES];

	ast_mutex_lock(&acelp_codec_lock);

	if (!state->initialized) {
		Init_Decod_Tetra();
		acelp_sdec_state_get(&state->sdec);
		acelp_postproc_state_init(&state->postproc);
		acelp_tetraop_state_get(&state->tetraop);
		state->initialized = 1;
	}

	acelp_tetraop_state_set(&state->tetraop);
	acelp_postproc_state_set(&state->postproc);
	acelp_sdec_state_set(&state->sdec);

	for (i = 0; i < frames; ++i) {
		const uint8_t *src_payload = payload_in + (i * ACELP_FRAME_BYTES);
		int16_t *dst = pcm_out + (i * ACELP_FRAME_SAMPLES);

		payload_to_serial(src_payload, serial);
		Bits2prm_Tetra(serial, parm);
		Decod_Tetra(parm, synth);
		Post_Process(synth, ACELP_FRAME_SAMPLES);
		memcpy(dst, synth, ACELP_FRAME_SAMPLES * sizeof(Word16));
	}

	acelp_sdec_state_get(&state->sdec);
	acelp_postproc_state_get(&state->postproc);
	acelp_tetraop_state_get(&state->tetraop);

	ast_mutex_unlock(&acelp_codec_lock);
	return 0;
}

static int acelp_samples_count(struct ast_frame *frame)
{
	return (frame->datalen / ACELP_FRAME_BYTES) * ACELP_FRAME_SAMPLES;
}

static int acelp_get_length(unsigned int samples)
{
	return (samples * 1000) / ACELP_SAMPLE_RATE;
}

static struct ast_codec acelp_codec = {
	.name = "acelp",
	.description = "TETRA ACELP",
	.type = AST_MEDIA_TYPE_AUDIO,
	.sample_rate = ACELP_SAMPLE_RATE,
	.minimum_ms = ACELP_FRAME_MS,
	.maximum_ms = ACELP_FRAME_MS,
	.default_ms = ACELP_FRAME_MS,
	.minimum_bytes = ACELP_FRAME_BYTES,
	.samples_count = acelp_samples_count,
	.get_length = acelp_get_length,
	.smooth = 0,
	.smoother_flags = 0,
	.quality = 70,
};

static struct ast_frame *acelp_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(ex_acelp),
		.samples = ACELP_FRAME_SAMPLES,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_acelp,
	};

	f.subclass.format = ast_format_acelp;
	return &f;
}

static int acelptolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct acelp_translator_pvt *tmp = pvt->pvt;
	int frames;
	int max_frames;
	int16_t *dst;

	if (f->datalen < ACELP_FRAME_BYTES) {
		return 0;
	}

	frames = f->datalen / ACELP_FRAME_BYTES;
	if (f->datalen % ACELP_FRAME_BYTES) {
		ast_log(LOG_WARNING, "Dropping %d trailing bytes for non-aligned ACELP frame\n",
			f->datalen % ACELP_FRAME_BYTES);
	}

	max_frames = (ACELP_BUFFER_SAMPLES - pvt->samples) / ACELP_FRAME_SAMPLES;
	if (max_frames <= 0) {
		ast_log(LOG_WARNING, "ACELP decoder buffer full\n");
		return -1;
	}
	if (frames > max_frames) {
		ast_log(LOG_WARNING, "ACELP decoder clipping %d frames due to output buffer limits\n",
			frames - max_frames);
		frames = max_frames;
	}

	dst = pvt->outbuf.i16 + pvt->samples;
	if (decode_payload_frames(&tmp->decoder, (const uint8_t *) f->data.ptr, frames, dst)) {
		return -1;
	}

	pvt->samples += frames * ACELP_FRAME_SAMPLES;
	pvt->datalen += frames * ACELP_FRAME_SAMPLES * (int) sizeof(int16_t);

	return 0;
}

static int lintoacelp_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{
	struct acelp_translator_pvt *tmp = pvt->pvt;
	int copy_samples;

	if (pvt->samples >= ACELP_BUFFER_SAMPLES) {
		ast_log(LOG_WARNING, "ACELP encoder buffer full\n");
		return -1;
	}

	copy_samples = f->samples;
	if (copy_samples > (ACELP_BUFFER_SAMPLES - pvt->samples)) {
		copy_samples = ACELP_BUFFER_SAMPLES - pvt->samples;
		ast_log(LOG_WARNING, "ACELP encoder clipped %d samples\n", f->samples - copy_samples);
	}

	memcpy(tmp->slin_buf + pvt->samples, f->data.ptr, copy_samples * (int) sizeof(int16_t));
	pvt->samples += copy_samples;

	return 0;
}

static struct ast_frame *lintoacelp_frameout(struct ast_trans_pvt *pvt)
{
	struct acelp_translator_pvt *tmp = pvt->pvt;
	struct ast_frame *result = NULL;
	struct ast_frame *last = NULL;
	uint8_t *payload_buf = NULL;
	int frames;
	int consumed_samples;
	int i;

	frames = pvt->samples / ACELP_FRAME_SAMPLES;
	if (frames <= 0) {
		return NULL;
	}

	payload_buf = ast_calloc(frames, ACELP_FRAME_BYTES);
	if (!payload_buf) {
		return NULL;
	}

	if (encode_linear_frames(&tmp->encoder, tmp->slin_buf, frames, payload_buf)) {
		ast_free(payload_buf);
		return NULL;
	}

	for (i = 0; i < frames; ++i) {
		struct ast_frame *current;
		const uint8_t *src_payload = payload_buf + (i * ACELP_FRAME_BYTES);

		memcpy(pvt->outbuf.uc, src_payload, ACELP_FRAME_BYTES);
		current = ast_trans_frameout(pvt, ACELP_FRAME_BYTES, ACELP_FRAME_SAMPLES);

		if (!current) {
			continue;
		}
		if (last) {
			AST_LIST_NEXT(last, frame_list) = current;
		} else {
			result = current;
		}
		last = current;
	}

	consumed_samples = frames * ACELP_FRAME_SAMPLES;
	pvt->samples -= consumed_samples;
	if (pvt->samples > 0) {
		memmove(tmp->slin_buf, tmp->slin_buf + consumed_samples,
			pvt->samples * (int) sizeof(int16_t));
	}

	ast_free(payload_buf);
	return result;
}

static struct ast_translator acelptolin = {
	.table_cost = AST_TRANS_COST_LY_LL_ORIGSAMP,
	.name = "acelptolin",
	.src_codec = {
		.name = "acelp",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = ACELP_SAMPLE_RATE,
	},
	.dst_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = ACELP_SAMPLE_RATE,
	},
	.format = "slin",
	.framein = acelptolin_framein,
	.sample = acelp_sample,
	.desc_size = sizeof(struct acelp_translator_pvt),
	.buffer_samples = ACELP_BUFFER_SAMPLES,
	.buf_size = ACELP_BUFFER_SAMPLES * (int) sizeof(int16_t),
};

static struct ast_translator lintoacelp = {
	.table_cost = AST_TRANS_COST_LL_LY_ORIGSAMP,
	.name = "lintoacelp",
	.src_codec = {
		.name = "slin",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = ACELP_SAMPLE_RATE,
	},
	.dst_codec = {
		.name = "acelp",
		.type = AST_MEDIA_TYPE_AUDIO,
		.sample_rate = ACELP_SAMPLE_RATE,
	},
	.format = "acelp",
	.framein = lintoacelp_framein,
	.frameout = lintoacelp_frameout,
	.sample = slin8_sample,
	.desc_size = sizeof(struct acelp_translator_pvt),
	.buffer_samples = ACELP_BUFFER_SAMPLES,
	.buf_size = ACELP_FRAME_BYTES,
};

static int unload_module(void)
{
	int res = 0;

	if (ast_format_acelp) {
		res |= ast_rtp_engine_unload_format(ast_format_acelp);
	}
	res |= ast_unregister_translator(&lintoacelp);
	res |= ast_unregister_translator(&acelptolin);

	ao2_cleanup(ast_format_acelp);
	ast_format_acelp = NULL;

	return res;
}

static int load_module(void)
{
	int res = 0;
	struct ast_codec *core_codec;

	res |= ast_codec_register(&acelp_codec);
	if (res) {
		ast_log(LOG_ERROR, "Failed to register ACELP codec\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	core_codec = ast_codec_get("acelp", AST_MEDIA_TYPE_AUDIO, ACELP_SAMPLE_RATE);
	if (!core_codec) {
		ast_log(LOG_ERROR, "Failed to fetch registered ACELP codec\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_acelp = ast_format_create_named("acelp", core_codec);
	ao2_cleanup(core_codec);
	if (!ast_format_acelp) {
		ast_log(LOG_ERROR, "Failed to create ACELP format\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	res |= ast_format_cache_set(ast_format_acelp);
	res |= ast_register_translator(&acelptolin);
	res |= ast_register_translator(&lintoacelp);
	res |= ast_rtp_engine_load_format(ast_format_acelp);

	if (res) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_verb(2, "Loaded codec_acelp_tetra\n");
	return AST_MODULE_LOAD_SUCCESS;
}

#undef AST_BUILDOPT_SUM
#define AST_BUILDOPT_SUM ""

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "TETRA ACELP/8000 Coder/Decoder",
	.load = load_module,
	.unload = unload_module);
