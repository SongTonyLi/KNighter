### Report Summary

File:| avcodec/ratecontrol.c  
---|---  
Warning:| line 432, column 35  
division by possibly zero aggregate factor  
  
### Annotated Source Code


9     |  * modify it under the terms of the GNU Lesser General Public
10    |  * License as published by the Free Software Foundation; either
11    |  * version 2.1 of the License, or (at your option) any later version.
12    |  *
13    |  * FFmpeg is distributed in the hope that it will be useful,
14    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
15    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
16    |  * Lesser General Public License for more details.
17    |  *
18    |  * You should have received a copy of the GNU Lesser General Public
19    |  * License along with FFmpeg; if not, write to the Free Software
20    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
21    |  */
22    |  
23    | /**
24    |  * @file
25    |  * Rate control for video encoders.
26    |  */
27    |  
28    | #include "libavutil/attributes.h"
29    | #include "libavutil/internal.h"
30    | #include "libavutil/mem.h"
31    |  
32    | #include "avcodec.h"
33    | #include "ratecontrol.h"
34    | #include "mpegvideoenc.h"
35    | #include "libavutil/eval.h"
36    |  
37    | void ff_write_pass1_stats(MPVMainEncContext *const m)
38    | {
39    |  const MPVEncContext *const s = &m->s;
40    |     snprintf(s->c.avctx->stats_out, 256,
41    |  "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d "
42    |  "fcode:%d bcode:%d mc-var:%"PRId64" var:%"PRId64" icount:%d hbits:%d;\n",
43    |              s->c.cur_pic.ptr->display_picture_number,
44    |              s->c.cur_pic.ptr->coded_picture_number,
45    |              s->c.pict_type,
46    |              s->c.cur_pic.ptr->f->quality,
47    |              s->i_tex_bits,
48    |              s->p_tex_bits,
49    |              s->mv_bits,
50    |              s->misc_bits,
51    |              s->f_code,
52    |              s->b_code,
53    |              m->mc_mb_var_sum,
54    |              m->mb_var_sum,
55    |              s->i_count,
56    |              m->header_bits);
57    | }
58    |  
59    | static AVRational get_fpsQ(AVCodecContext *avctx)
60    | {
61    |  if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
62    |  return avctx->framerate;
63    |  
64    |  return av_inv_q(avctx->time_base);
65    | }
66    |  
67    | static double get_fps(AVCodecContext *avctx)
68    | {
69    |  return av_q2d(get_fpsQ(avctx));
70    | }
71    |  
72    | static inline double qp2bits(const RateControlEntry *rce, double qp)
73    | {
74    |  if (qp <= 0.0) {
75    |         av_log(NULL, AV_LOG_ERROR, "qp<=0.0\n");
76    |     }
77    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / qp;
78    | }
79    |  
80    | static double qp2bits_cb(void *rce, double qp)
81    | {
82    |  return qp2bits(rce, qp);
83    | }
84    |  
85    | static inline double bits2qp(const RateControlEntry *rce, double bits)
86    | {
87    |  if (bits < 0.9) {
88    |         av_log(NULL, AV_LOG_ERROR, "bits<0.9\n");
89    |     }
90    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / bits;
91    | }
92    |  
283   |         rce->pict_type == AV_PICTURE_TYPE_B,
284   |         rcc->qscale_sum[pict_type] / (double)rcc->frame_count[pict_type],
285   |         avctx->qcompress,
286   |         rcc->i_cplx_sum[AV_PICTURE_TYPE_I] / (double)rcc->frame_count[AV_PICTURE_TYPE_I],
287   |         rcc->i_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
288   |         rcc->p_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
289   |         rcc->p_cplx_sum[AV_PICTURE_TYPE_B] / (double)rcc->frame_count[AV_PICTURE_TYPE_B],
290   |         (rcc->i_cplx_sum[pict_type] + rcc->p_cplx_sum[pict_type]) / (double)rcc->frame_count[pict_type],
291   |         0
292   |     };
293   |  
294   |     bits = av_expr_eval(rcc->rc_eq_eval, const_values, rce);
295   |  if (isnan(bits)) {
296   |         av_log(avctx, AV_LOG_ERROR, "Error evaluating rc_eq \"%s\"\n", rcc->rc_eq);
297   |  return -1;
298   |     }
299   |  
300   |     rcc->pass1_rc_eq_output_sum += bits;
301   |     bits *= rate_factor;
302   |  if (bits < 0.0)
303   |         bits = 0.0;
304   |     bits += 1.0; // avoid 1/0 issues
305   |  
306   |  /* user override */
307   |  for (i = 0; i < avctx->rc_override_count; i++) {
308   |         RcOverride *rco = avctx->rc_override;
309   |  if (rco[i].start_frame > frame_num)
310   |  continue;
311   |  if (rco[i].end_frame < frame_num)
312   |  continue;
313   |  
314   |  if (rco[i].qscale)
315   |             bits = qp2bits(rce, rco[i].qscale);  // FIXME move at end to really force it?
316   |  else
317   |             bits *= rco[i].quality_factor;
318   |     }
319   |  
320   |     q = bits2qp(rce, bits);
321   |  
322   |  /* I/B difference */
323   |  if (pict_type == AV_PICTURE_TYPE_I && avctx->i_quant_factor < 0.0)
324   |         q = -q * avctx->i_quant_factor + avctx->i_quant_offset;
325   |  else if (pict_type == AV_PICTURE_TYPE_B && avctx->b_quant_factor < 0.0)
326   |         q = -q * avctx->b_quant_factor + avctx->b_quant_offset;
327   |  if (q < 1)
328   |         q = 1;
329   |  
330   |  return q;
331   | }
332   |  
333   | static int init_pass2(MPVMainEncContext *const m)
334   | {
335   |  RateControlContext *const rcc = &m->rc_context;
336   |     MPVEncContext      *const   s = &m->s;
337   |     AVCodecContext     *const avctx = s->c.avctx;
338   |  int i, toobig;
339   |     AVRational fps         = get_fpsQ(avctx);
340   |  double complexity[5]   = { 0 }; // approximate bits at quant=1
341   |     uint64_t const_bits[5] = { 0 }; // quantizer independent bits
342   |     uint64_t all_const_bits;
343   |     uint64_t all_available_bits = av_rescale_q(m->bit_rate,
344   |                                                (AVRational){rcc->num_entries,1},
345   |                                                fps);
346   |  double rate_factor          = 0;
347   |  double step;
348   |  const int filter_size = (int)(avctx->qblur * 4) | 1;
349   |  double expected_bits = 0; // init to silence gcc warning
350   |  double *qscale, *blurred_qscale, qscale_sum;
351   |  
352   |  /* find complexity & const_bits & decide the pict_types */
353   |  for (i = 0; i < rcc->num_entries; i++) {
    1Assuming 'i' is >= field 'num_entries'→
    2←Loop condition is false. Execution continues on line 367→
354   |         RateControlEntry *rce = &rcc->entry[i];
355   |  
356   |         rce->new_pict_type                = rce->pict_type;
357   |         rcc->i_cplx_sum[rce->pict_type]  += rce->i_tex_bits * rce->qscale;
358   |         rcc->p_cplx_sum[rce->pict_type]  += rce->p_tex_bits * rce->qscale;
359   |         rcc->mv_bits_sum[rce->pict_type] += rce->mv_bits;
360   |         rcc->frame_count[rce->pict_type]++;
361   |  
362   |         complexity[rce->new_pict_type] += (rce->i_tex_bits + rce->p_tex_bits) *
363   |                                           (double)rce->qscale;
364   |         const_bits[rce->new_pict_type] += rce->mv_bits + rce->misc_bits;
365   |     }
366   |  
367   |  all_const_bits = const_bits[AV_PICTURE_TYPE_I] +
368   |                      const_bits[AV_PICTURE_TYPE_P] +
369   |                      const_bits[AV_PICTURE_TYPE_B];
370   |  
371   |  if (all_available_bits < all_const_bits) {
    3←Assuming 'all_available_bits' is >= 'all_const_bits'→
    4←Taking false branch→
372   |         av_log(avctx, AV_LOG_ERROR, "requested bitrate is too low\n");
373   |  return -1;
374   |     }
375   |  
376   |  qscale         = av_malloc_array(rcc->num_entries, sizeof(double));
377   |     blurred_qscale = av_malloc_array(rcc->num_entries, sizeof(double));
378   |  if (!qscale || !blurred_qscale) {
    5←Assuming 'qscale' is non-null→
    6←Assuming 'blurred_qscale' is non-null→
    7←Taking false branch→
379   |         av_free(qscale);
380   |         av_free(blurred_qscale);
381   |  return AVERROR(ENOMEM);
382   |     }
383   |  toobig = 0;
384   |  
385   |  for (step = 256 * 256; step > 0.0000001; step *= 0.5) {
    8←Loop condition is true.  Entering loop body→
386   |  expected_bits = 0;
387   |         rate_factor  += step;
388   |  
389   |         rcc->buffer_index = avctx->rc_buffer_size / 2;
390   |  
391   |  /* find qscale */
392   |  for (i = 0; i8.1'i' is >= field 'num_entries' < rcc->num_entries; i++) {
    9←Loop condition is false. Execution continues on line 398→
393   |  const RateControlEntry *rce = &rcc->entry[i];
394   |  
395   |             qscale[i] = get_qscale(m, &rcc->entry[i], rate_factor, i);
396   |             rcc->last_qscale_for[rce->pict_type] = qscale[i];
397   |         }
398   |  av_assert0(filter_size % 2 == 1);
    10←Assuming the condition is true→
    11←Taking false branch→
399   |  
400   |  /* fixed I/B QP relative to P mode */
401   |  for (i = FFMAX(0, rcc->num_entries - 300); i < rcc->num_entries; i++) {
    12←Loop condition is false.  Exiting loop→
    13←Assuming the condition is false→
    14←'?' condition is false→
    15←Assuming 'i' is < field 'num_entries'→
    16←Loop condition is true.  Entering loop body→
    17←Assuming 'i' is >= field 'num_entries'→
    18←Loop condition is false. Execution continues on line 407→
402   |  const RateControlEntry *rce = &rcc->entry[i];
403   |  
404   |  qscale[i] = get_diff_limited_q(m, rce, qscale[i]);
405   |  }
406   |  
407   |  for (i = rcc->num_entries - 1; i >= 0; i--) {
    19←Assuming 'i' is >= 0→
    20←Loop condition is true.  Entering loop body→
    21←Assuming 'i' is < 0→
    22←Loop condition is false. Execution continues on line 414→
408   |  const RateControlEntry *rce = &rcc->entry[i];
409   |  
410   |  qscale[i] = get_diff_limited_q(m, rce, qscale[i]);
411   |  }
412   |  
413   |  /* smooth curve */
414   |  for (i = 0; i < rcc->num_entries; i++) {
    23←Assuming 'i' is < field 'num_entries'→
    24←Loop condition is true.  Entering loop body→
415   |  const RateControlEntry *rce = &rcc->entry[i];
416   |  const int pict_type   = rce->new_pict_type;
417   |  int j;
418   |  double q = 0.0, sum = 0.0;
419   |  
420   |  for (j = 0; j < filter_size; j++) {
    25←Assuming 'j' is >= 'filter_size'→
    26←Loop condition is false. Execution continues on line 432→
421   |  int index    = i + j - filter_size / 2;
422   |  double d     = index - i;
423   |  double coeff = avctx->qblur == 0 ? 1.0 : exp(-d * d / (avctx->qblur * avctx->qblur));
424   |  
425   |  if (index < 0 || index >= rcc->num_entries)
426   |  continue;
427   |  if (pict_type != rcc->entry[index].new_pict_type)
428   |  continue;
429   |                 q   += qscale[index] * coeff;
430   |                 sum += coeff;
431   |             }
432   |  blurred_qscale[i] = q / sum;
    27←division by possibly zero aggregate factor
433   |         }
434   |  
435   |  /* find expected bits */
436   |  for (i = 0; i < rcc->num_entries; i++) {
437   |             RateControlEntry *rce = &rcc->entry[i];
438   |  double bits;
439   |  
440   |             rce->new_qscale = modify_qscale(m, rce, blurred_qscale[i], i);
441   |  
442   |             bits  = qp2bits(rce, rce->new_qscale) + rce->mv_bits + rce->misc_bits;
443   |             bits += 8 * ff_vbv_update(m, bits);
444   |  
445   |             rce->expected_bits = expected_bits;
446   |             expected_bits     += bits;
447   |         }
448   |  
449   |  ff_dlog(avctx,
450   |  "expected_bits: %f all_available_bits: %d rate_factor: %f\n",
451   |  expected_bits, (int)all_available_bits, rate_factor);
452   |  if (expected_bits > all_available_bits) {
453   |             rate_factor -= step;
454   |             ++toobig;
455   |         }
456   |     }
457   |     av_free(qscale);
458   |     av_free(blurred_qscale);
459   |  
460   |  /* check bitrate calculations and print info */
461   |     qscale_sum = 0.0;
462   |  for (i = 0; i < rcc->num_entries; i++) {