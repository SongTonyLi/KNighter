### Report Summary

File:| avfilter/af_aspectralstats.c  
---|---  
Warning:| line 397, column 16  
division by possibly zero aggregate factor  
  
### Annotated Source Code


331   |     den = logf(size);
332   |  if (den <= FLT_EPSILON)
333   |  return 1.f;
334   |  return -num / den;
335   | }
336   |  
337   | static float spectral_flatness(const float *const spectral, int size, int max_freq)
338   | {
339   |  float num = 0.f, den = 0.f;
340   |  
341   |  for (int n = 0; n < size; n++) {
342   |  float v = FLT_EPSILON + spectral[n];
343   |         num += logf(v);
344   |         den += v;
345   |     }
346   |  
347   |     num /= size;
348   |     den /= size;
349   |     num = expf(num);
350   |  if (den <= FLT_EPSILON)
351   |  return 0.f;
352   |  return num / den;
353   | }
354   |  
355   | static float spectral_crest(const float *const spectral, int size, int max_freq)
356   | {
357   |  float max = 0.f, mean = 0.f;
358   |  
359   |  for (int n = 0; n < size; n++) {
360   |         max = fmaxf(max, spectral[n]);
361   |         mean += spectral[n];
362   |     }
363   |  
364   |     mean /= size;
365   |  if (mean <= FLT_EPSILON)
366   |  return 0.f;
367   |  return max / mean;
368   | }
369   |  
370   | static float spectral_flux(const float *const spectral, const float *const prev_spectral,
371   |  int size, int max_freq)
372   | {
373   |  float sum = 0.f;
374   |  
375   |  for (int n = 0; n < size; n++)
376   |         sum += sqrf(spectral[n] - prev_spectral[n]);
377   |  
378   |  return sqrtf(sum);
379   | }
380   |  
381   | static float spectral_slope(const float *const spectral, int size, int max_freq)
382   | {
383   |  const float mean_freq = size * 0.5f;
384   |  float mean_spectral = 0.f, num = 0.f, den = 0.f;
385   |  
386   |  for (int n = 0; n30.1'n' is >= 'size' < size; n++)
    31←Loop condition is false. Execution continues on line 388→
387   |         mean_spectral += spectral[n];
388   |  mean_spectral /= size;
389   |  
390   |  for (int n = 0; n31.1'n' is >= 'size' < size; n++) {
    32←Loop condition is false. Execution continues on line 395→
391   |         num += ((n - mean_freq) / mean_freq) * (spectral[n] - mean_spectral);
392   |         den += sqrf((n - mean_freq) / mean_freq);
393   |     }
394   |  
395   |  if (fabsf(den) <= FLT_EPSILON)
    33←Assuming the condition is false→
    34←Taking false branch→
396   |  return 0.f;
397   |  return num / den;
    35←division by possibly zero aggregate factor
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
    16←Assuming the condition is false→
    17←Taking false branch→
482   |             stats->skewness = spectral_skewness(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
483   |  if (s->measure & MEASURE_KURTOSIS)
    18←Assuming the condition is false→
    19←Taking false branch→
484   |             stats->kurtosis = spectral_kurtosis(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
485   |  if (s->measure & MEASURE_ENTROPY)
    20←Assuming the condition is false→
    21←Taking false branch→
486   |             stats->entropy  = spectral_entropy(magnitude, s->win_size / 2, in->sample_rate / 2);
487   |  if (s->measure & MEASURE_FLATNESS)
    22←Assuming the condition is false→
    23←Taking false branch→
488   |             stats->flatness = spectral_flatness(magnitude, s->win_size / 2, in->sample_rate / 2);
489   |  if (s->measure & MEASURE_CREST)
    24←Assuming the condition is false→
    25←Taking false branch→
490   |             stats->crest    = spectral_crest(magnitude, s->win_size / 2, in->sample_rate / 2);
491   |  if (s->measure & MEASURE_FLUX)
    26←Assuming the condition is false→
    27←Taking false branch→
492   |             stats->flux     = spectral_flux(magnitude, prev_magnitude, s->win_size / 2, in->sample_rate / 2);
493   |  if (s->measure & MEASURE_SLOPE)
    28←Assuming the condition is true→
    29←Taking true branch→
494   |  stats->slope    = spectral_slope(magnitude, s->win_size / 2, in->sample_rate / 2);
    30←Calling 'spectral_slope'→
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
513   |  int ret;
514   |  
515   |  if (av_frame_is_writable(in)) {
516   |         out = in;
517   |     } else {
518   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
519   |  if (!out) {
520   |             av_frame_free(&in);
521   |  return AVERROR(ENOMEM);
522   |         }
523   |         ret = av_frame_copy_props(out, in);
524   |  if (ret < 0)