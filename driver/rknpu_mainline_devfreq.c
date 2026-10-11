// SPDX-License-Identifier: GPL-2.0
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/* R6S initial mainline port: real regulator/clock OPP, fixed 200MHz. */
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include "rknpu_drv.h"
#include "rknpu_devfreq.h"
#define R6S_NPU_RATE 200000000UL
static int target(struct device *dev, unsigned long *freq, u32 flags)
{
 struct rknpu_device *npu = dev_get_drvdata(dev);
 int ret;
 if (*freq != R6S_NPU_RATE) return -EOPNOTSUPP;
 mutex_lock(&npu->frequency_lock);
 ret = dev_pm_opp_set_rate(dev, R6S_NPU_RATE);
 if (!ret) {
  npu->current_freq = clk_get_rate(npu->clks[0].clk);
  npu->current_volt = regulator_get_voltage(npu->vdd);
 }
 mutex_unlock(&npu->frequency_lock);
 return ret;
}
static int current_rate(struct device *dev, unsigned long *freq)
{
 struct rknpu_device *npu = dev_get_drvdata(dev);
 *freq = clk_get_rate(npu->clks[0].clk);
 return 0;
}
static struct devfreq_dev_profile profile = {
 .initial_freq = R6S_NPU_RATE, .polling_ms = 0,
 .target = target, .get_cur_freq = current_rate,
};
void rknpu_devfreq_lock(struct rknpu_device *npu)
{ mutex_lock(&npu->frequency_lock); }
void rknpu_devfreq_unlock(struct rknpu_device *npu)
{ mutex_unlock(&npu->frequency_lock); }
int rknpu_devfreq_init(struct rknpu_device *npu)
{
 const char *clocks[] = { "clk_npu", NULL };
 const char *regulators[] = { "rknpu", NULL };
 struct dev_pm_opp_config config = { .clk_names = clocks,
                                     .regulator_names = regulators };
 struct dev_pm_opp *opp;
 unsigned long freq = R6S_NPU_RATE;
 int ret;
 npu->opp_config_token = dev_pm_opp_set_config(npu->dev, &config);
 if (npu->opp_config_token < 0) return npu->opp_config_token;
 ret = dev_pm_opp_of_add_table(npu->dev);
 if (ret) goto clear;
 npu->opp_table_added = true;
 opp = dev_pm_opp_find_freq_exact(npu->dev, freq, true);
 if (IS_ERR(opp)) { ret = PTR_ERR(opp); goto table; }
 dev_pm_opp_put(opp);
 ret = target(npu->dev, &freq, 0);
 if (ret) goto table;
 npu->devfreq = devfreq_add_device(npu->dev, &profile,
                                  DEVFREQ_GOV_USERSPACE, NULL);
 if (IS_ERR(npu->devfreq)) {
  ret = PTR_ERR(npu->devfreq); npu->devfreq = NULL; goto table;
 }
 return 0;
table:
 dev_pm_opp_of_remove_table(npu->dev); npu->opp_table_added = false;
clear:
 dev_pm_opp_clear_config(npu->opp_config_token);
 return ret;
}
void rknpu_devfreq_remove(struct rknpu_device *npu)
{
 if (npu->devfreq) { devfreq_remove_device(npu->devfreq); npu->devfreq = NULL; }
 if (npu->opp_table_added) {
  dev_pm_opp_of_remove_table(npu->dev); npu->opp_table_added = false;
  dev_pm_opp_clear_config(npu->opp_config_token);
 }
}
int rknpu_devfreq_runtime_suspend(struct device *dev)
{
 struct rknpu_device *npu = dev_get_drvdata(dev);
 return npu->devfreq ? devfreq_suspend_device(npu->devfreq) : 0;
}
int rknpu_devfreq_runtime_resume(struct device *dev)
{
 struct rknpu_device *npu = dev_get_drvdata(dev);
 return npu->devfreq ? devfreq_resume_device(npu->devfreq) : 0;
}
