/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/clk.h>

#include "seninf_clk.h"

static struct SENINF_CLK_CTRL gseninf_mclk_name[SENINF_CLK_IDX_MAX_NUM] = {
	{"SCP_SYS_MM0"},
	{"SCP_SYS_CAM"},
	{"CLK_CAM_SENINF"},
	{"CLK_TOP_CAMTG_SEL"},
	{"CLK_TOP_CAMTG2_SEL"},
	{"CLK_TOP_CAMTG3_SEL"},
	{"CLK_TOP_CAMTG4_SEL"},
	{"CLK_TOP_UNIVPLL_D416"},
	{"CLK_TOP_UNIVPLL_D208"},
	{"CLK_TOP_UNIVPLL_D104"},
	{"CLK_TOP_CLK26M"},
	{"CLK_TOP_UNIVPLL_D52"},
	{"CLK_TOP_UNIVPLL2_D16"},
};

static enum SENINF_CLK_MCLK_FREQ gseninf_clk_freq[SENINF_CLK_IDX_FREQ_IDX_NUM] = {
	SENINF_CLK_MCLK_FREQ_6MHZ,
	SENINF_CLK_MCLK_FREQ_12MHZ,
	SENINF_CLK_MCLK_FREQ_24MHZ,
	SENINF_CLK_MCLK_FREQ_26MHZ,
	SENINF_CLK_MCLK_FREQ_48MHZ,
	SENINF_CLK_MCLK_FREQ_52MHZ,
};

static inline void seninf_clk_check(struct SENINF_CLK *pclk)
{
	int i;

	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++)
		WARN_ON(IS_ERR(pclk->mclk_sel[i]));
}

/*******************************************************************************
* Common Clock Framework (CCF)
********************************************************************************/
enum SENINF_RETURN seninf_clk_init(struct SENINF_CLK *pclk)
{
	int i;

	if (pclk->pplatform_device == NULL) {
		PK_PR_ERR("[%s] pdev is null\n", __func__);
		return SENINF_RETURN_ERROR;
	}
	/* get all possible using clocks */
	for (i = 0; i < SENINF_CLK_IDX_MAX_NUM; i++) {
		pclk->mclk_sel[i] =
		    devm_clk_get(&pclk->pplatform_device->dev, gseninf_mclk_name[i].pctrl);
		atomic_set(&pclk->enable_cnt[i], 0);

		if (IS_ERR(pclk->mclk_sel[i])) {
			PK_PR_ERR("cannot get %d clock\n", i);
			return SENINF_RETURN_ERROR;
		}
	}

#ifdef CONFIG_PM_WAKELOCKS
	wakeup_source_init(&pclk->seninf_wake_lock, "seninf_lock_wakelock");
#else
	wake_lock_init(&pclk->seninf_wake_lock, WAKE_LOCK_SUSPEND, "seninf_lock_wakelock");
#endif
	atomic_set(&pclk->wakelock_cnt, 0);

	return SENINF_RETURN_SUCCESS;
}

int seninf_clk_set(struct SENINF_CLK *pclk, ACDK_SENSOR_MCLK_STRUCT *pmclk)
{
	int i, ret = 0;

	if (pmclk->TG >= SENINF_CLK_TG_MAX_NUM ||
	    pmclk->freq > SENINF_CLK_MCLK_FREQ_MAX || pmclk->freq < SENINF_CLK_MCLK_FREQ_MIN) {
		PK_PR_ERR("[CAMERA SENSOR]kdSetSensorMclk out of range, tg = %d, freq = %d\n",
			  pmclk->TG, pmclk->freq);
		return -EFAULT;
	}

	PK_DBG("[CAMERA SENSOR] CCF kdSetSensorMclk on= %d, freq= %d, TG= %d\n",
	       pmclk->on, pmclk->freq, pmclk->TG);

	seninf_clk_check(pclk);

	for (i = 0; pmclk->freq != gseninf_clk_freq[i]; i++)
		;

	if (pmclk->on) {
		/* Workaround for timestamp: TG1 always ON */
		if (clk_prepare_enable(pclk->mclk_sel[SENINF_CLK_IDX_TG_TOP_CAMTG_SEL]))
			PK_PR_ERR("[CAMERA SENSOR] failed tg=%d\n",
				  SENINF_CLK_IDX_TG_TOP_CAMTG_SEL);
		else
			atomic_inc(&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_CAMTG_SEL]);

		if (clk_prepare_enable(pclk->mclk_sel[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM]))
			PK_PR_ERR("[CAMERA SENSOR] failed tg=%d\n", pmclk->TG);
		else
			atomic_inc(&pclk->enable_cnt[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM]);

		if (clk_prepare_enable(pclk->mclk_sel[i + SENINF_CLK_IDX_FREQ_MIN_NUM]))
			PK_PR_ERR("[CAMERA SENSOR] failed freq idx= %d\n", i);
		else
			atomic_inc(&pclk->enable_cnt[i + SENINF_CLK_IDX_FREQ_MIN_NUM]);

		ret = clk_set_parent(pclk->mclk_sel[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM],
				     pclk->mclk_sel[i + SENINF_CLK_IDX_FREQ_MIN_NUM]);
	} else {
		if (atomic_read(&pclk->enable_cnt[i + SENINF_CLK_IDX_FREQ_MIN_NUM]) > 0) {
			clk_disable_unprepare(pclk->mclk_sel[i + SENINF_CLK_IDX_FREQ_MIN_NUM]);
			atomic_dec(&pclk->enable_cnt[i + SENINF_CLK_IDX_FREQ_MIN_NUM]);
		}

		if (atomic_read(&pclk->enable_cnt[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM]) > 0) {
			clk_disable_unprepare(pclk->mclk_sel[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM]);
			atomic_dec(&pclk->enable_cnt[pmclk->TG + SENINF_CLK_IDX_TG_MIN_NUM]);
		}

		/* Workaround for timestamp: TG1 always ON */
		if (atomic_read(&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_CAMTG_SEL]) > 0) {
			clk_disable_unprepare(pclk->mclk_sel[SENINF_CLK_IDX_TG_TOP_CAMTG_SEL]);
			atomic_dec(&pclk->enable_cnt[SENINF_CLK_IDX_TG_TOP_CAMTG_SEL]);
		}
	}

	return ret;
}

void seninf_clk_open(struct SENINF_CLK *pclk)
{
	MINT32 i;

	PK_DBG("open\n");

	if (atomic_inc_return(&pclk->wakelock_cnt) == 1) {
#ifdef CONFIG_PM_WAKELOCKS
		__pm_stay_awake(&pclk->seninf_wake_lock);
#else
		wake_lock(&pclk->seninf_wake_lock);
#endif
	}

	for (i = SENINF_CLK_IDX_SYS_MIN_NUM; i < SENINF_CLK_IDX_SYS_MAX_NUM; i++) {
		if (clk_prepare_enable(pclk->mclk_sel[i]))
			PK_PR_ERR("[CAMERA SENSOR] failed sys idx= %d\n", i);
		else
			atomic_inc(&pclk->enable_cnt[i]);
	}
}

void seninf_clk_release(struct SENINF_CLK *pclk)
{
	MINT32 i = SENINF_CLK_IDX_MAX_NUM;

	PK_DBG("release\n");

	do {
		i--;
		for (; atomic_read(&pclk->enable_cnt[i]) > 0;) {
			clk_disable_unprepare(pclk->mclk_sel[i]);
			atomic_dec(&pclk->enable_cnt[i]);
		}
	} while (i);

	if (atomic_dec_and_test(&pclk->wakelock_cnt)) {
#ifdef CONFIG_PM_WAKELOCKS
		__pm_relax(&pclk->seninf_wake_lock);
#else
		wake_unlock(&pclk->seninf_wake_lock);
#endif
	}
}

unsigned int seninf_clk_get_meter(struct SENINF_CLK *pclk, unsigned int clk)
{
#if !defined(CONFIG_FPGA_EARLY_PORTING)
	if (clk == 36) {
		PK_DBG("SCP_SYS_CAM = %lu\n",
			clk_get_rate(pclk->mclk_sel[SENINF_CLK_IDX_SYS_SCP_SYS_CAM]));
		PK_DBG("CLK_CAM_SENINF = %lu\n",
			clk_get_rate(pclk->mclk_sel[SENINF_CLK_IDX_SYS_CLK_CAM_SENINF]));
	}
	return mt_get_ckgen_freq(clk);
#else
	return 0;
#endif
}

