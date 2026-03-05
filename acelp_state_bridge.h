#ifndef ACELP_STATE_BRIDGE_H
#define ACELP_STATE_BRIDGE_H

#include "source.h"

#define ACELP_SCOD_L_TOTAL 290
#define ACELP_SCOD_OLD_WSP_LEN 383
#define ACELP_SCOD_OLD_EXC_LEN 398
#define ACELP_SCOD_AI_ZERO_LEN 71
#define ACELP_SCOD_P 10
#define ACELP_SCOD_DIM_RR 32

#define ACELP_SDEC_OLD_EXC_LEN 398
#define ACELP_SDEC_P 10
#define ACELP_SDEC_PARM_SIZE 23

#define ACELP_POSTPROC_A_LEN 11

typedef struct acelp_scod_state {
	Word16 old_speech[ACELP_SCOD_L_TOTAL];
	Word16 old_wsp[ACELP_SCOD_OLD_WSP_LEN];
	Word16 old_exc[ACELP_SCOD_OLD_EXC_LEN];
	Word16 ai_zero[ACELP_SCOD_AI_ZERO_LEN];
	Word16 f_gamma1[ACELP_SCOD_P];
	Word16 f_gamma2[ACELP_SCOD_P];
	Word16 f_gamma3[ACELP_SCOD_P];
	Word16 f_gamma4[ACELP_SCOD_P];
	Word16 lspold[ACELP_SCOD_P];
	Word16 lspnew[ACELP_SCOD_P];
	Word16 lspnew_q[ACELP_SCOD_P];
	Word16 lspold_q[ACELP_SCOD_P];
	Word16 mem_syn[ACELP_SCOD_P];
	Word16 mem_w0[ACELP_SCOD_P];
	Word16 mem_w[ACELP_SCOD_P];
	Word16 rr[ACELP_SCOD_DIM_RR][ACELP_SCOD_DIM_RR];
	Word16 last_ener_cod;
	Word16 last_ener_pit;
} acelp_scod_state_t;

typedef struct acelp_sdec_state {
	Word16 old_exc[ACELP_SDEC_OLD_EXC_LEN];
	Word16 f_gamma3[ACELP_SDEC_P];
	Word16 f_gamma4[ACELP_SDEC_P];
	Word16 lspold[ACELP_SDEC_P];
	Word16 lspnew[ACELP_SDEC_P];
	Word16 mem_syn[ACELP_SDEC_P];
	Word16 old_parm[ACELP_SDEC_PARM_SIZE];
	Word16 old_t0;
	Word16 last_ener_cod;
	Word16 last_ener_pit;
} acelp_sdec_state_t;

typedef struct acelp_preproc_state {
	Word16 y_hi;
	Word16 y_lo;
	Word16 x0;
} acelp_preproc_state_t;

typedef struct acelp_postproc_state {
	Word16 old_a[ACELP_POSTPROC_A_LEN];
} acelp_postproc_state_t;

typedef struct acelp_tetraop_state {
	Flag overflow;
	Flag carry;
} acelp_tetraop_state_t;

void acelp_scod_state_get(acelp_scod_state_t *state);
void acelp_scod_state_set(const acelp_scod_state_t *state);

void acelp_sdec_state_get(acelp_sdec_state_t *state);
void acelp_sdec_state_set(const acelp_sdec_state_t *state);

void acelp_preproc_state_get(acelp_preproc_state_t *state);
void acelp_preproc_state_set(const acelp_preproc_state_t *state);

void acelp_postproc_state_get(acelp_postproc_state_t *state);
void acelp_postproc_state_set(const acelp_postproc_state_t *state);

void acelp_tetraop_state_get(acelp_tetraop_state_t *state);
void acelp_tetraop_state_set(const acelp_tetraop_state_t *state);

#endif
