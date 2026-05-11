### Report Summary

File:| avfilter/af_afftdn.c  
---|---  
Warning:| line 462, column 36  
division by possibly zero aggregate factor  
  
### Annotated Source Code


303   | static double limit_gain(double a, double b)
304   | {
305   |  if (a > 1.0)
306   |  return (b * a - 1.0) / (b + a - 2.0);
307   |  if (a < 1.0)
308   |  return (b * a - 2.0 * a + 1.0) / (b - a);
309   |  return 1.0;
310   | }
311   |  
312   | static void spectral_flatness(AudioFFTDeNoiseContext *s, const double *const spectral,
313   |  double floor, int len, double *rnum, double *rden)
314   | {
315   |  double num = 0., den = 0.;
316   |  int size = 0;
317   |  
318   |  for (int n = 0; n < len; n++) {
319   |  const double v = spectral[n];
320   |  if (v > floor) {
321   |             num += log(v);
322   |             den += v;
323   |             size++;
324   |         }
325   |     }
326   |  
327   |     size = FFMAX(size, 1);
328   |  
329   |     num /= size;
330   |     den /= size;
331   |  
332   |     num = exp(num);
333   |  
334   |     *rnum = num;
335   |     *rden = den;
336   | }
337   |  
338   | static void set_parameters(AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch, int update_var, int update_auto_var);
339   |  
340   | static double floor_offset(const double *S, int size, double mean)
341   | {
342   |  double offset = 0.0;
343   |  
344   |  for (int n = 0; n < size; n++) {
345   |  const double p = S[n] - mean;
346   |  
347   |         offset = fmax(offset, fabs(p));
348   |     }
349   |  
350   |  return offset / mean;
351   | }
352   |  
353   | static void process_frame(AVFilterContext *ctx,
354   |                           AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch,
355   |  double *prior, double *prior_band_excit, int track_noise)
356   | {
357   |  AVFilterLink *outlink = ctx->outputs[0];
358   |     FilterLink      *outl = ff_filter_link(outlink);
359   |  const double *abs_var = dnch->abs_var;
360   |  const double ratio = outl->frame_count_out ? s->ratio : 1.0;
    5←Assuming field 'frame_count_out' is 0→
    6←'?' condition is false→
361   |  const double rratio = 1. - ratio;
362   |  const int *bin2band = s->bin2band;
363   |  double *noisy_data = dnch->noisy_data;
364   |  double *band_excit = dnch->band_excit;
365   |  double *band_amt = dnch->band_amt;
366   |  double *smoothed_gain = dnch->smoothed_gain;
367   |     AVComplexDouble *fft_data_dbl = dnch->fft_out;
368   |     AVComplexFloat *fft_data_flt = dnch->fft_out;
369   |  double *gain = dnch->gain;
370   |  
371   |  for (int i = 0; i < s->bin_count; i++) {
    7←Assuming 'i' is >= field 'bin_count'→
    8←Loop condition is false. Execution continues on line 395→
372   |  double sqr_new_gain, new_gain, power, mag, mag_abs_var, new_mag_abs_var;
373   |  
374   |  switch (s->format) {
375   |  case AV_SAMPLE_FMT_FLTP:
376   |             noisy_data[i] = mag = hypot(fft_data_flt[i].re, fft_data_flt[i].im);
377   |  break;
378   |  case AV_SAMPLE_FMT_DBLP:
379   |             noisy_data[i] = mag = hypot(fft_data_dbl[i].re, fft_data_dbl[i].im);
380   |  break;
381   |  default:
382   |  av_assert0(0);
383   |         }
384   |  
385   |         power = mag * mag;
386   |         mag_abs_var = power / abs_var[i];
387   |         new_mag_abs_var = ratio * prior[i] + rratio * fmax(mag_abs_var - 1.0, 0.0);
388   |         new_gain = new_mag_abs_var / (1.0 + new_mag_abs_var);
389   |         sqr_new_gain = new_gain * new_gain;
390   |         prior[i] = mag_abs_var * sqr_new_gain;
391   |         dnch->clean_data[i] = power * sqr_new_gain;
392   |         gain[i] = new_gain;
393   |     }
394   |  
395   |  if (track_noise) {
    9←Assuming 'track_noise' is 0→
    10←Taking false branch→
396   |  double flatness, num, den;
397   |  
398   |         spectral_flatness(s, noisy_data, s->floor, s->bin_count, &num, &den);
399   |  
400   |         flatness = num / den;
401   |  if (flatness > 0.8) {
402   |  const double offset = s->floor_offset * floor_offset(noisy_data, s->bin_count, den);
403   |  const double new_floor = av_clipd(10.0 * log10(den) - 100.0 + offset, -90., -20.);
404   |  
405   |             dnch->noise_floor = 0.1 * new_floor + dnch->noise_floor * 0.9;
406   |             set_parameters(s, dnch, 1, 1);
407   |         }
408   |     }
409   |  
410   |  for (int i = 0; i < s->number_of_bands; i++) {
    11←Assuming 'i' is >= field 'number_of_bands'→
    12←Loop condition is false. Execution continues on line 415→
411   |         band_excit[i] = 0.0;
412   |         band_amt[i] = 0.0;
413   |     }
414   |  
415   |  for (int i = 0; i12.1'i' is >= field 'bin_count' < s->bin_count; i++)
    13←Loop condition is false. Execution continues on line 418→
416   |         band_excit[bin2band[i]] += dnch->clean_data[i];
417   |  
418   |  for (int i = 0; i13.1'i' is >= field 'number_of_bands' < s->number_of_bands; i++) {
    14←Loop condition is false. Execution continues on line 425→
419   |         band_excit[i] = fmax(band_excit[i],
420   |                              s->band_alpha[i] * band_excit[i] +
421   |                              s->band_beta[i] * prior_band_excit[i]);
422   |         prior_band_excit[i] = band_excit[i];
423   |     }
424   |  
425   |  for (int j = 0, i = 0; j14.1'j' is >= field 'number_of_bands' < s->number_of_bands; j++) {
    15←Loop condition is false. Execution continues on line 431→
426   |  for (int k = 0; k < s->number_of_bands; k++) {
427   |             band_amt[j] += dnch->spread_function[i++] * band_excit[k];
428   |         }
429   |     }
430   |  
431   |  for (int i = 0; i15.1'i' is >= field 'bin_count' < s->bin_count; i++)
    16←Loop condition is false. Execution continues on line 434→
432   |         dnch->amt[i] = band_amt[bin2band[i]];
433   |  
434   |  for (int i = 0; i16.1'i' is >= field 'bin_count' < s->bin_count; i++) {
    17←Loop condition is false. Execution continues on line 446→
435   |  if (dnch->amt[i] > abs_var[i]) {
436   |             gain[i] = 1.0;
437   |         } else if (dnch->amt[i] > dnch->min_abs_var[i]) {
438   |  const double limit = sqrt(abs_var[i] / dnch->amt[i]);
439   |  
440   |             gain[i] = limit_gain(gain[i], limit);
441   |         } else {
442   |             gain[i] = limit_gain(gain[i], dnch->max_gain);
443   |         }
444   |     }
445   |  
446   |  memcpy(smoothed_gain, gain, s->bin_count * sizeof(*smoothed_gain));
447   |  if (s->gain_smooth > 0) {
    18←Assuming field 'gain_smooth' is > 0→
    19←Taking true branch→
448   |  const int r = s->gain_smooth;
449   |  
450   |  for (int i = r; i < s->bin_count - r; i++) {
    20←Assuming the condition is true→
    21←Loop condition is true.  Entering loop body→
451   |  const double gc = gain[i];
452   |  double num = 0., den = 0.;
453   |  
454   |  for (int j = -r; j <= r; j++) {
    22←Assuming 'j' is > 'r'→
    23←Loop condition is false. Execution continues on line 462→
455   |  const double g = gain[i + j];
456   |  const double d = 1. - fabs(g - gc);
457   |  
458   |                 num += g * d;
459   |                 den += d;
460   |             }
461   |  
462   |  smoothed_gain[i] = num / den;
    24←division by possibly zero aggregate factor
463   |         }
464   |     }
465   |  
466   |  switch (s->format) {
467   |  case AV_SAMPLE_FMT_FLTP:
468   |  for (int i = 0; i < s->bin_count; i++) {
469   |  const float new_gain = smoothed_gain[i];
470   |  
471   |             fft_data_flt[i].re *= new_gain;
472   |             fft_data_flt[i].im *= new_gain;
473   |         }
474   |  break;
475   |  case AV_SAMPLE_FMT_DBLP:
476   |  for (int i = 0; i < s->bin_count; i++) {
477   |  const double new_gain = smoothed_gain[i];
478   |  
479   |             fft_data_dbl[i].re *= new_gain;
480   |             fft_data_dbl[i].im *= new_gain;
481   |         }
482   |  break;
483   |     }
484   | }
485   |  
486   | static double freq2bark(double x)
487   | {
488   |  double d = x / 7500.0;
489   |  
490   |  return 13.0 * atan(7.6E-4 * x) + 3.5 * atan(d * d);
491   | }
492   |  
1001  |         dnch->noise_band_var[i] /= dnch->noise_band_norm[i];
1002  |         dnch->noise_band_var[i] -= dnch->noise_band_avr[i] * dnch->noise_band_avr[i] +
1003  |                                    dnch->noise_band_avi[i] * dnch->noise_band_avi[i];
1004  |         dnch->noise_band_auto_var[i] = dnch->noise_band_var[i];
1005  |         sample_noise[i] = 10.0 * log10(dnch->noise_band_var[i] / s->floor) - 100.0;
1006  |     }
1007  |  if (s->noise_band_count < NB_PROFILE_BANDS) {
1008  |  for (int i = s->noise_band_count; i < NB_PROFILE_BANDS; i++)
1009  |             sample_noise[i] = sample_noise[i - 1];
1010  |     }
1011  | }
1012  |  
1013  | static void set_noise_profile(AVFilterContext *ctx,
1014  |                               DeNoiseChannel *dnch,
1015  |  double *sample_noise)
1016  | {
1017  |     AudioFFTDeNoiseContext *s = ctx->priv;
1018  |  double new_band_noise[NB_PROFILE_BANDS];
1019  |  double temp[NB_PROFILE_BANDS];
1020  |  double sum = 0.0;
1021  |  
1022  |  for (int m = 0; m < NB_PROFILE_BANDS; m++)
1023  |         temp[m] = sample_noise[m];
1024  |  
1025  |  for (int m = 0, i = 0; m < SOLVE_SIZE; m++) {
1026  |         sum = 0.0;
1027  |  for (int n = 0; n < NB_PROFILE_BANDS; n++)
1028  |             sum += s->matrix_b[i++] * temp[n];
1029  |         s->vector_b[m] = sum;
1030  |     }
1031  |     solve(s->matrix_a, s->vector_b, SOLVE_SIZE);
1032  |  for (int m = 0, i = 0; m < NB_PROFILE_BANDS; m++) {
1033  |         sum = 0.0;
1034  |  for (int n = 0; n < SOLVE_SIZE; n++)
1035  |             sum += s->matrix_c[i++] * s->vector_b[n];
1036  |         temp[m] = sum;
1037  |     }
1038  |  
1039  |     reduce_mean(temp);
1040  |  
1041  |     av_log(ctx, AV_LOG_INFO, "bn=");
1042  |  for (int m = 0; m < NB_PROFILE_BANDS; m++) {
1043  |         new_band_noise[m] = temp[m];
1044  |         new_band_noise[m] = av_clipd(new_band_noise[m], -24.0, 24.0);
1045  |         av_log(ctx, AV_LOG_INFO, "%f ", new_band_noise[m]);
1046  |     }
1047  |     av_log(ctx, AV_LOG_INFO, "\n");
1048  |     memcpy(dnch->band_noise, new_band_noise, sizeof(new_band_noise));
1049  | }
1050  |  
1051  | static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
1052  | {
1053  |  AudioFFTDeNoiseContext *s = ctx->priv;
1054  |     AVFrame *in = arg;
1055  |  const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
1056  |  const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
1057  |  const int window_length = s->window_length;
1058  |  const double *window = s->window;
1059  |  
1060  |  for (int ch = start; ch < end; ch++) {
    1Assuming 'ch' is < 'end'→
    2←Loop condition is true.  Entering loop body→
1061  |  DeNoiseChannel *dnch = &s->dnch[ch];
1062  |  const double *src_dbl = (const double *)in->extended_data[ch];
1063  |  const float *src_flt = (const float *)in->extended_data[ch];
1064  |  double *dst = dnch->out_samples;
1065  |  double *fft_in_dbl = dnch->fft_in;
1066  |  float *fft_in_flt = dnch->fft_in;
1067  |  
1068  |  switch (s->format) {
    3←'Default' branch taken. Execution continues on line 1085→
1069  |  case AV_SAMPLE_FMT_FLTP:
1070  |  for (int m = 0; m < window_length; m++)
1071  |                 fft_in_flt[m] = window[m] * src_flt[m] * (1LL << 23);
1072  |  
1073  |  for (int m = window_length; m < s->fft_length2; m++)
1074  |                 fft_in_flt[m] = 0.f;
1075  |  break;
1076  |  case AV_SAMPLE_FMT_DBLP:
1077  |  for (int m = 0; m < window_length; m++)
1078  |                 fft_in_dbl[m] = window[m] * src_dbl[m] * (1LL << 23);
1079  |  
1080  |  for (int m = window_length; m < s->fft_length2; m++)
1081  |                 fft_in_dbl[m] = 0.;
1082  |  break;
1083  |         }
1084  |  
1085  |  dnch->tx_fn(dnch->fft, dnch->fft_out, dnch->fft_in, s->sample_size);
1086  |  
1087  |  process_frame(ctx, s, dnch,
    4←Calling 'process_frame'→
1088  |  dnch->prior,
1089  |  dnch->prior_band_excit,
1090  |  s->track_noise);
1091  |  
1092  |         dnch->itx_fn(dnch->ifft, dnch->fft_in, dnch->fft_out, s->complex_sample_size);
1093  |  
1094  |  switch (s->format) {
1095  |  case AV_SAMPLE_FMT_FLTP:
1096  |  for (int m = 0; m < window_length; m++)
1097  |                 dst[m] += s->window[m] * fft_in_flt[m] / (1LL << 23);
1098  |  break;
1099  |  case AV_SAMPLE_FMT_DBLP:
1100  |  for (int m = 0; m < window_length; m++)
1101  |                 dst[m] += s->window[m] * fft_in_dbl[m] / (1LL << 23);
1102  |  break;
1103  |         }
1104  |     }
1105  |  
1106  |  return 0;
1107  | }
1108  |  
1109  | static int output_frame(AVFilterLink *inlink, AVFrame *in)
1110  | {
1111  |     AVFilterContext *ctx = inlink->dst;
1112  |     AVFilterLink *outlink = ctx->outputs[0];
1113  |     AudioFFTDeNoiseContext *s = ctx->priv;
1114  |  const int output_mode = ctx->is_disabled ? IN_MODE : s->output_mode;
1115  |  const int offset = s->window_length - s->sample_advance;
1116  |     AVFrame *out;
1117  |  
1118  |  for (int ch = 0; ch < s->channels; ch++) {
1119  |         uint8_t *src = (uint8_t *)s->winframe->extended_data[ch];
1120  |  