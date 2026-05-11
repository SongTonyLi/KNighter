### Report Summary

File:| avfilter/af_aspectralstats.c  
---|---  
Warning:| line 304, column 16  
division by possibly zero aggregate factor  
  
### Annotated Source Code


236   |         sum += spectral[n];
237   |  
238   |  return sum / size;
239   | }
240   |  
241   | static float sqrf(float a)
242   | {
243   |  return a * a;
244   | }
245   |  
246   | static float spectral_variance(const float *const spectral, int size, int max_freq, float mean)
247   | {
248   |  float sum = 0.f;
249   |  
250   |  for (int n = 0; n < size; n++)
251   |         sum += sqrf(spectral[n] - mean);
252   |  
253   |  return sum / size;
254   | }
255   |  
256   | static float spectral_centroid(const float *const spectral, int size, int max_freq)
257   | {
258   |  const float scale = max_freq / (float)size;
259   |  float num = 0.f, den = 0.f;
260   |  
261   |  for (int n = 0; n < size; n++) {
262   |         num += spectral[n] * n * scale;
263   |         den += spectral[n];
264   |     }
265   |  
266   |  if (den <= FLT_EPSILON)
267   |  return 1.f;
268   |  return num / den;
269   | }
270   |  
271   | static float spectral_spread(const float *const spectral, int size, int max_freq, float centroid)
272   | {
273   |  const float scale = max_freq / (float)size;
274   |  float num = 0.f, den = 0.f;
275   |  
276   |  for (int n = 0; n < size; n++) {
277   |         num += spectral[n] * sqrf(n * scale - centroid);
278   |         den += spectral[n];
279   |     }
280   |  
281   |  if (den <= FLT_EPSILON)
282   |  return 1.f;
283   |  return sqrtf(num / den);
284   | }
285   |  
286   | static float cbrf(float a)
287   | {
288   |  return a * a * a;
289   | }
290   |  
291   | static float spectral_skewness(const float *const spectral, int size, int max_freq, float centroid, float spread)
292   | {
293   |  const float scale = max_freq / (float)size;
294   |  float num = 0.f, den = 0.f;
295   |  
296   |  for (int n = 0; n18.1'n' is >= 'size' < size; n++) {
    19←Loop condition is false. Execution continues on line 301→
297   |         num += spectral[n] * cbrf(n * scale - centroid);
298   |         den += spectral[n];
299   |     }
300   |  
301   |  den *= cbrf(spread);
302   |  if (den <= FLT_EPSILON)
    20←Assuming 'den' is > FLT_EPSILON→
    21←Taking false branch→
303   |  return 1.f;
304   |  return num / den;
    22←division by possibly zero aggregate factor
305   | }
306   |  
307   | static float spectral_kurtosis(const float *const spectral, int size, int max_freq, float centroid, float spread)
308   | {
309   |  const float scale = max_freq / (float)size;
310   |  float num = 0.f, den = 0.f;
311   |  
312   |  for (int n = 0; n < size; n++) {
313   |         num += spectral[n] * sqrf(sqrf(n * scale - centroid));
314   |         den += spectral[n];
315   |     }
316   |  
317   |     den *= sqrf(sqrf(spread));
318   |  if (den <= FLT_EPSILON)
319   |  return 1.f;
320   |  return num / den;
321   | }
322   |  
323   | static float spectral_entropy(const float *const spectral, int size, int max_freq)
324   | {
325   |  float num = 0.f, den = 0.f;
326   |  
327   |  for (int n = 0; n < size; n++) {
328   |         num += spectral[n] * logf(spectral[n] + FLT_EPSILON);
329   |     }
330   |  
331   |     den = logf(size);
332   |  if (den <= FLT_EPSILON)
333   |  return 1.f;
334   |  return -num / den;
385   |  
386   |  for (int n = 0; n < size; n++)
387   |         mean_spectral += spectral[n];
388   |     mean_spectral /= size;
389   |  
390   |  for (int n = 0; n < size; n++) {
391   |         num += ((n - mean_freq) / mean_freq) * (spectral[n] - mean_spectral);
392   |         den += sqrf((n - mean_freq) / mean_freq);
393   |     }
394   |  
395   |  if (fabsf(den) <= FLT_EPSILON)
396   |  return 0.f;
397   |  return num / den;
398   | }
399   |  
400   | static float spectral_decrease(const float *const spectral, int size, int max_freq)
401   | {
402   |  float num = 0.f, den = 0.f;
403   |  
404   |  for (int n = 1; n < size; n++) {
405   |         num += (spectral[n] - spectral[0]) / n;
406   |         den += spectral[n];
407   |     }
408   |  
409   |  if (den <= FLT_EPSILON)
410   |  return 0.f;
411   |  return num / den;
412   | }
413   |  
414   | static float spectral_rolloff(const float *const spectral, int size, int max_freq)
415   | {
416   |  const float scale = max_freq / (float)size;
417   |  float norm = 0.f, sum = 0.f;
418   |  int idx = 0.f;
419   |  
420   |  for (int n = 0; n < size; n++)
421   |         norm += spectral[n];
422   |     norm *= 0.85f;
423   |  
424   |  for (int n = 0; n < size; n++) {
425   |         sum += spectral[n];
426   |  if (sum >= norm) {
427   |             idx = n;
428   |  break;
429   |         }
430   |     }
431   |  
432   |  return idx * scale;
433   | }
434   |  
435   | static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
436   | {
437   |  AudioSpectralStatsContext *s = ctx->priv;
438   |  const float *window_func_lut = s->window_func_lut;
439   |     AVFrame *in = arg;
440   |  const int channels = s->nb_channels;
441   |  const int start = (channels * jobnr) / nb_jobs;
442   |  const int end = (channels * (jobnr+1)) / nb_jobs;
443   |  const int offset = s->win_size - s->hop_size;
444   |  
445   |  for (int ch = start; ch < end; ch++) {
    1Assuming 'ch' is < 'end'→
    2←Loop condition is true.  Entering loop body→
446   |  float *window = (float *)s->window->extended_data[ch];
447   |         ChannelSpectralStats *stats = &s->stats[ch];
448   |         AVComplexFloat *fft_out = s->fft_out[ch];
449   |         AVComplexFloat *fft_in = s->fft_in[ch];
450   |  float *magnitude = s->magnitude[ch];
451   |  float *prev_magnitude = s->prev_magnitude[ch];
452   |  const float scale = 1.f / s->win_size;
453   |  
454   |         memmove(window, &window[s->hop_size], offset * sizeof(float));
455   |         memcpy(&window[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
456   |         memset(&window[offset + in->nb_samples], 0, (s->hop_size - in->nb_samples) * sizeof(float));
457   |  
458   |  for (int n = 0; n < s->win_size; n++) {
    3←Assuming 'n' is >= field 'win_size'→
    4←Loop condition is false. Execution continues on line 463→
459   |             fft_in[n].re = window[n] * window_func_lut[n];
460   |             fft_in[n].im = 0;
461   |         }
462   |  
463   |  s->tx_fn(s->fft[ch], fft_out, fft_in, sizeof(*fft_in));
464   |  
465   |  for (int n = 0; n < s->win_size / 2; n++) {
    5←Assuming the condition is false→
    6←Loop condition is false. Execution continues on line 470→
466   |             fft_out[n].re *= scale;
467   |             fft_out[n].im *= scale;
468   |         }
469   |  
470   |  for (int n = 0; n < s->win_size / 2; n++)
    7←Loop condition is false. Execution continues on line 473→
471   |             magnitude[n] = hypotf(fft_out[n].re, fft_out[n].im);
472   |  
473   |  if (s->measure & (MEASURE_MEAN | MEASURE_VARIANCE))
    8←Assuming the condition is false→
    9←Taking false branch→
474   |             stats->mean     = spectral_mean(magnitude, s->win_size / 2, in->sample_rate / 2);
475   |  if (s->measure & MEASURE_VARIANCE)
    10←Assuming the condition is false→
    11←Taking false branch→
476   |             stats->variance = spectral_variance(magnitude, s->win_size / 2, in->sample_rate / 2, stats->mean);
477   |  if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS | MEASURE_CENTROID))
    12←Assuming the condition is false→
    13←Taking false branch→
478   |             stats->centroid = spectral_centroid(magnitude, s->win_size / 2, in->sample_rate / 2);
479   |  if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS))
    14←Assuming the condition is false→
    15←Taking false branch→
480   |             stats->spread   = spectral_spread(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid);
481   |  if (s->measure & MEASURE_SKEWNESS)
    16←Assuming the condition is true→
    17←Taking true branch→
482   |  stats->skewness = spectral_skewness(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
    18←Calling 'spectral_skewness'→
483   |  if (s->measure & MEASURE_KURTOSIS)
484   |             stats->kurtosis = spectral_kurtosis(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
485   |  if (s->measure & MEASURE_ENTROPY)
486   |             stats->entropy  = spectral_entropy(magnitude, s->win_size / 2, in->sample_rate / 2);
487   |  if (s->measure & MEASURE_FLATNESS)
488   |             stats->flatness = spectral_flatness(magnitude, s->win_size / 2, in->sample_rate / 2);
489   |  if (s->measure & MEASURE_CREST)
490   |             stats->crest    = spectral_crest(magnitude, s->win_size / 2, in->sample_rate / 2);
491   |  if (s->measure & MEASURE_FLUX)
492   |             stats->flux     = spectral_flux(magnitude, prev_magnitude, s->win_size / 2, in->sample_rate / 2);
493   |  if (s->measure & MEASURE_SLOPE)
494   |             stats->slope    = spectral_slope(magnitude, s->win_size / 2, in->sample_rate / 2);
495   |  if (s->measure & MEASURE_DECREASE)
496   |             stats->decrease = spectral_decrease(magnitude, s->win_size / 2, in->sample_rate / 2);
497   |  if (s->measure & MEASURE_ROLLOFF)
498   |             stats->rolloff  = spectral_rolloff(magnitude, s->win_size / 2, in->sample_rate / 2);
499   |  
500   |         memcpy(prev_magnitude, magnitude, s->win_size * sizeof(float));
501   |     }
502   |  
503   |  return 0;
504   | }
505   |  
506   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
507   | {
508   |     AVFilterContext *ctx = inlink->dst;
509   |     AVFilterLink *outlink = ctx->outputs[0];
510   |     AudioSpectralStatsContext *s = ctx->priv;
511   |     AVDictionary **metadata;
512   |     AVFrame *out;