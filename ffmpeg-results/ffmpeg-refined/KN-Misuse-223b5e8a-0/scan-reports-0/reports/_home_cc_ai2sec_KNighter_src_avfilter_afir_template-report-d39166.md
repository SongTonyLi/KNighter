### Report Summary

File:| avfilter/afir_template.c  
---|---  
Warning:| line 67, column 22  
division by possibly zero aggregate factor  
  
### Annotated Source Code


257   |     seg->tempout = ff_get_audio_buffer(ctx->inputs[0], seg->block_size);
258   |     seg->buffer = ff_get_audio_buffer(ctx->inputs[0], seg->part_size);
259   |     seg->input  = ff_get_audio_buffer(ctx->inputs[0], seg->input_size);
260   |     seg->output = ff_get_audio_buffer(ctx->inputs[0], seg->part_size * 5);
261   |  if (!seg->buffer || !seg->sumin || !seg->sumout || !seg->blockout ||
262   |         !seg->input || !seg->output || !seg->tempin || !seg->tempout)
263   |  return AVERROR(ENOMEM);
264   |  
265   |  return 0;
266   | }
267   |  
268   | static void uninit_segment(AVFilterContext *ctx, AudioFIRSegment *seg)
269   | {
270   |     AudioFIRContext *s = ctx->priv;
271   |  
272   |  if (seg->ctx) {
273   |  for (int ch = 0; ch < s->nb_channels; ch++)
274   |             av_tx_uninit(&seg->ctx[ch]);
275   |     }
276   |     av_freep(&seg->ctx);
277   |  
278   |  if (seg->tx) {
279   |  for (int ch = 0; ch < s->nb_channels; ch++)
280   |             av_tx_uninit(&seg->tx[ch]);
281   |     }
282   |     av_freep(&seg->tx);
283   |  
284   |  if (seg->itx) {
285   |  for (int ch = 0; ch < s->nb_channels; ch++)
286   |             av_tx_uninit(&seg->itx[ch]);
287   |     }
288   |     av_freep(&seg->itx);
289   |  
290   |     av_freep(&seg->output_offset);
291   |     av_freep(&seg->part_index);
292   |  
293   |     av_frame_free(&seg->tempin);
294   |     av_frame_free(&seg->tempout);
295   |     av_frame_free(&seg->blockout);
296   |     av_frame_free(&seg->sumin);
297   |     av_frame_free(&seg->sumout);
298   |     av_frame_free(&seg->buffer);
299   |     av_frame_free(&seg->input);
300   |     av_frame_free(&seg->output);
301   |     seg->input_size = 0;
302   |  
303   |  for (int i = 0; i < MAX_IR_STREAMS; i++)
304   |         av_frame_free(&seg->coeff);
305   | }
306   |  
307   | static int convert_coeffs(AVFilterContext *ctx, int selir)
308   | {
309   |  AudioFIRContext *s = ctx->priv;
310   |  int ret, nb_taps, cur_nb_taps;
311   |  
312   |  if (!s->nb_taps[selir]) {
    1Assuming the condition is false→
    2←Taking false branch→
313   |  int part_size, max_part_size;
314   |  int left, offset = 0;
315   |  
316   |         s->nb_taps[selir] = ff_inlink_queued_samples(ctx->inputs[1 + selir]);
317   |  if (s->nb_taps[selir] <= 0)
318   |  return AVERROR(EINVAL);
319   |  
320   |  if (s->minp > s->maxp)
321   |             s->maxp = s->minp;
322   |  
323   |  if (s->nb_segments[selir])
324   |  goto skip;
325   |  
326   |         left = s->nb_taps[selir];
327   |         part_size = 1 << av_log2(s->minp);
328   |         max_part_size = 1 << av_log2(s->maxp);
329   |  
330   |  for (int i = 0; left > 0; i++) {
331   |  int step = (part_size == max_part_size) ? INT_MAX : 1 + (i == 0);
332   |  int nb_partitions = FFMIN(step, (left + part_size - 1) / part_size);
333   |  
334   |             s->nb_segments[selir] = i + 1;
335   |             ret = init_segment(ctx, &s->seg[selir][i], selir, offset, nb_partitions, part_size, i);
336   |  if (ret < 0)
337   |  return ret;
338   |             offset += nb_partitions * part_size;
339   |             s->max_offset[selir] = offset;
340   |             left -= nb_partitions * part_size;
341   |             part_size *= 2;
342   |             part_size = FFMIN(part_size, max_part_size);
343   |         }
344   |     }
345   |  
346   | skip:
347   |  if (!s->ir[selir]) {
    3←Assuming the condition is false→
    4←Taking false branch→
348   |         ret = ff_inlink_consume_samples(ctx->inputs[1 + selir], s->nb_taps[selir], s->nb_taps[selir], &s->ir[selir]);
349   |  if (ret < 0)
350   |  return ret;
351   |  if (ret == 0)
352   |  return AVERROR_BUG;
353   |     }
354   |  
355   |  cur_nb_taps  = s->ir[selir]->nb_samples;
356   |     nb_taps      = cur_nb_taps;
357   |  
358   |  if (!s->norm_ir[selir] || s->norm_ir[selir]->nb_samples < nb_taps) {
    5←Assuming the condition is false→
    6←Assuming 'nb_taps' is <= field 'nb_samples'→
    7←Taking false branch→
359   |         av_frame_free(&s->norm_ir[selir]);
360   |         s->norm_ir[selir] = ff_get_audio_buffer(ctx->inputs[0], FFALIGN(nb_taps, 8));
361   |  if (!s->norm_ir[selir])
362   |  return AVERROR(ENOMEM);
363   |     }
364   |  
365   |  av_log(ctx, AV_LOG_DEBUG, "nb_taps: %d\n", cur_nb_taps);
366   |     av_log(ctx, AV_LOG_DEBUG, "nb_segments: %d\n", s->nb_segments[selir]);
367   |  
368   |  switch (s->format) {
    8←Control jumps to 'case AV_SAMPLE_FMT_DBLP:'  at line 409→
369   |  case AV_SAMPLE_FMT_FLTP:
370   |  for (int ch = 0; ch < s->nb_channels; ch++) {
371   |  const float *tsrc = (const float *)s->ir[selir]->extended_data[!s->one2many * ch];
372   |  
373   |             s->ch_gain[ch] = ir_gain_float(ctx, s, nb_taps, tsrc);
374   |         }
375   |  
376   |  if (s->ir_link) {
377   |  float gain = +INFINITY;
378   |  
379   |  for (int ch = 0; ch < s->nb_channels; ch++)
380   |                 gain = fminf(gain, s->ch_gain[ch]);
381   |  
382   |  for (int ch = 0; ch < s->nb_channels; ch++)
383   |                 s->ch_gain[ch] = gain;
384   |         }
385   |  
386   |  for (int ch = 0; ch < s->nb_channels; ch++) {
387   |  const float *tsrc = (const float *)s->ir[selir]->extended_data[!s->one2many * ch];
388   |  float *time = (float *)s->norm_ir[selir]->extended_data[ch];
389   |  
390   |             memcpy(time, tsrc, sizeof(*time) * nb_taps);
391   |  for (int i = FFMAX(1, s->length * nb_taps); i < nb_taps; i++)
392   |                 time[i] = 0;
393   |  
394   |             ir_scale_float(ctx, s, nb_taps, ch, time, s->ch_gain[ch]);
395   |  
396   |  for (int n = 0; n < s->nb_segments[selir]; n++) {
397   |                 AudioFIRSegment *seg = &s->seg[selir][n];
398   |  
399   |  if (!seg->coeff)
400   |                     seg->coeff = ff_get_audio_buffer(ctx->inputs[0], seg->nb_partitions * seg->coeff_size * 2);
401   |  if (!seg->coeff)
402   |  return AVERROR(ENOMEM);
403   |  
404   |  for (int i = 0; i < seg->nb_partitions; i++)
405   |                     convert_channel_float(ctx, s, ch, seg, i, selir);
406   |             }
407   |         }
408   |  break;
409   |  case AV_SAMPLE_FMT_DBLP:
410   |  for (int ch = 0; ch < s->nb_channels; ch++) {
    9←Assuming 'ch' is < field 'nb_channels'→
    10←Loop condition is true.  Entering loop body→
411   |  const double *tsrc = (const double *)s->ir[selir]->extended_data[!s->one2many * ch];
    11←Assuming field 'one2many' is not equal to 0→
412   |  
413   |  s->ch_gain[ch] = ir_gain_double(ctx, s, nb_taps, tsrc);
    12←Calling 'ir_gain_double'→
414   |         }
415   |  
416   |  if (s->ir_link) {
417   |  double gain = +INFINITY;
418   |  
419   |  for (int ch = 0; ch < s->nb_channels; ch++)
420   |                 gain = fmin(gain, s->ch_gain[ch]);
421   |  
422   |  for (int ch = 0; ch < s->nb_channels; ch++)
423   |                 s->ch_gain[ch] = gain;
424   |         }
425   |  
426   |  for (int ch = 0; ch < s->nb_channels; ch++) {
427   |  const double *tsrc = (const double *)s->ir[selir]->extended_data[!s->one2many * ch];
428   |  double *time = (double *)s->norm_ir[selir]->extended_data[ch];
429   |  
430   |             memcpy(time, tsrc, sizeof(*time) * nb_taps);
431   |  for (int i = FFMAX(1, s->length * nb_taps); i < nb_taps; i++)
432   |                 time[i] = 0;
433   |  
434   |             ir_scale_double(ctx, s, nb_taps, ch, time, s->ch_gain[ch]);
435   |  
436   |  for (int n = 0; n < s->nb_segments[selir]; n++) {
437   |                 AudioFIRSegment *seg = &s->seg[selir][n];
438   |  
439   |  if (!seg->coeff)
440   |                     seg->coeff = ff_get_audio_buffer(ctx->inputs[0], seg->nb_partitions * seg->coeff_size * 2);
441   |  if (!seg->coeff)
442   |  return AVERROR(ENOMEM);
443   |  
7     |  * modify it under the terms of the GNU Lesser General Public
8     |  * License as published by the Free Software Foundation; either
9     |  * version 2.1 of the License, or (at your option) any later version.
10    |  *
11    |  * FFmpeg is distributed in the hope that it will be useful,
12    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
13    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
14    |  * Lesser General Public License for more details.
15    |  *
16    |  * You should have received a copy of the GNU Lesser General Public
17    |  * License along with FFmpeg; if not, write to the Free Software
18    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
19    |  */
20    |  
21    | #include "libavutil/tx.h"
22    | #include "avfilter.h"
23    | #include "audio.h"
24    |  
25    | #undef ctype
26    | #undef ftype
27    | #undef SQRT
28    | #undef HYPOT
29    | #undef SAMPLE_FORMAT
30    | #undef TX_TYPE
31    | #undef FABS
32    | #undef POW
33    | #if DEPTH == 32
34    | #define SAMPLE_FORMAT float
35    | #define SQRT sqrtf
36    | #define HYPOT hypotf
37    | #define ctype AVComplexFloat
38    | #define ftype float
39    | #define TX_TYPE AV_TX_FLOAT_RDFT
40    | #define FABS fabsf
41    | #define POW powf
42    | #else
43    | #define SAMPLE_FORMAT double
44    | #define SQRT sqrt
45    | #define HYPOT hypot
46    | #define ctype AVComplexDouble
47    | #define ftype double
48    | #define TX_TYPE AV_TX_DOUBLE_RDFT
49    | #define FABS fabs
50    | #define POW pow
51    | #endif
52    |  
53    | #define fn3(a,b)   a##_##b
54    | #define fn2(a,b) fn3(a,b)
55    | #define fn(a) fn2(a, SAMPLE_FORMAT)
56    |  
57    | static ftype fn(ir_gain)(AVFilterContext *ctx, AudioFIRContext *s,
58    |  int cur_nb_taps, const ftype *time)
59    | {
60    |  ftype ch_gain, sum = 0;
61    |  
62    |  if (s->ir_norm < 0.f) {
    13←Assuming the condition is false→
    14←Taking false branch→
63    |         ch_gain = 1;
64    |     } else if (s->ir_norm == 0.f) {
    15←Assuming the condition is true→
    16←Taking true branch→
65    |  for (int i = 0; i < cur_nb_taps; i++)
    17←Assuming 'i' is >= 'cur_nb_taps'→
    18←Loop condition is false. Execution continues on line 67→
66    |             sum += time[i];
67    |  ch_gain = 1. / sum;
    19←division by possibly zero aggregate factor
68    |     } else {
69    |  ftype ir_norm = s->ir_norm;
70    |  
71    |  for (int i = 0; i < cur_nb_taps; i++)
72    |             sum += POW(FABS(time[i]), ir_norm);
73    |         ch_gain = 1. / POW(sum, 1. / ir_norm);
74    |     }
75    |  
76    |  return ch_gain;
77    | }
78    |  
79    | static void fn(ir_scale)(AVFilterContext *ctx, AudioFIRContext *s,
80    |  int cur_nb_taps, int ch,
81    |  ftype *time, ftype ch_gain)
82    | {
83    |  if (ch_gain != 1. || s->ir_gain != 1.) {
84    |  ftype gain = ch_gain * s->ir_gain;
85    |  
86    |         av_log(ctx, AV_LOG_DEBUG, "ch%d gain %f\n", ch, gain);
87    | #if DEPTH == 32
88    |         s->fdsp->vector_fmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 4));
89    | #else
90    |         s->fdsp->vector_dmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 8));
91    | #endif
92    |     }
93    | }
94    |  
95    | static void fn(convert_channel)(AVFilterContext *ctx, AudioFIRContext *s, int ch,
96    |                                 AudioFIRSegment *seg, int coeff_partition, int selir)
97    | {