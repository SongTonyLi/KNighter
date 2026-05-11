### Report Summary

File:| avfilter/vf_ssim360.c  
---|---  
Warning:| line 538, column 30  
division by possibly zero aggregate factor  
  
### Annotated Source Code


459   | static double
460   | ssim360_endn_8bit(const int (*sum0)[4], const int (*sum1)[4], int width,
461   |  double *density_map, int map_width, double *total_weight)
462   | {
463   |  double ssim360 = 0.0, weight;
464   |  
465   |  for (int i = 0; i < width; i++) {
466   |         weight = density_map ? density_map[(int) ((0.5 + i) / width * map_width)] : 1.0;
467   |         ssim360 += weight * ssim360_end1(
468   |             sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
469   |             sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
470   |             sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
471   |             sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3]);
472   |         *total_weight += weight;
473   |     }
474   |  return ssim360;
475   | }
476   |  
477   | static double
478   | ssim360_plane_16bit(uint8_t *main, int main_stride,
479   |                     uint8_t *ref, int ref_stride,
480   |  int width, int height, void *temp,
481   |  int max, Map2D density)
482   | {
483   |  int z = 0;
484   |  double ssim360 = 0.0;
485   |     int64_t (*sum0)[4] = temp;
486   |     int64_t (*sum1)[4] = sum0 + (width >> 2) + 3;
487   |  double total_weight = 0.0;
488   |  
489   |     width >>= 2;
490   |     height >>= 2;
491   |  
492   |  for (int y = 1; y < height; y++) {
493   |  for (; z <= y; z++) {
494   |  FFSWAP(void*, sum0, sum1);
495   |             ssim360_4x4xn_16bit(&main[4 * z * main_stride], main_stride,
496   |                                 &ref[4 * z * ref_stride], ref_stride,
497   |                                 sum0, width);
498   |         }
499   |         ssim360 += ssim360_endn_16bit(
500   |             (const int64_t (*)[4])sum0, (const int64_t (*)[4])sum1,
501   |             width - 1, max,
502   |             density.value ? density.value + density.w * ((int) ((z - 1.0) / height * density.h)) : NULL,
503   |             density.w, &total_weight);
504   |     }
505   |  
506   |  return (double) (ssim360 / total_weight);
507   | }
508   |  
509   | static double
510   | ssim360_plane_8bit(uint8_t *main, int main_stride,
511   |                    uint8_t *ref, int ref_stride,
512   |  int width, int height, void *temp,
513   |  int max, Map2D density)
514   | {
515   |  int z = 0;
516   |  double ssim360 = 0.0;
517   |  int (*sum0)[4] = temp;
518   |  int (*sum1)[4] = sum0 + (width >> 2) + 3;
519   |  double total_weight = 0.0;
520   |  
521   |     width >>= 2;
522   |     height >>= 2;
523   |  
524   |  for (int y = 1; y < height; y++) {
    1Assuming 'y' is >= 'height'→
    2←Loop condition is false. Execution continues on line 538→
525   |  for (; z <= y; z++) {
526   |  FFSWAP(void*, sum0, sum1);
527   |             ssim360_4x4xn_8bit(
528   |                 &main[4 * z * main_stride], main_stride,
529   |                 &ref[4 * z * ref_stride], ref_stride,
530   |                 sum0, width);
531   |         }
532   |         ssim360 += ssim360_endn_8bit(
533   |             (const int (*)[4])sum0, (const int (*)[4])sum1, width - 1,
534   |             density.value ? density.value + density.w * ((int) ((z - 1.0) / height * density.h)) : NULL,
535   |             density.w, &total_weight);
536   |     }
537   |  
538   |  return (double) (ssim360 / total_weight);
    3←division by possibly zero aggregate factor
539   | }
540   |  
541   | static double ssim360_db(double ssim360, double weight)
542   | {
543   |  return 10 * log10(weight / (weight - ssim360));
544   | }
545   |  
546   | static int get_bilinear_sample(const uint8_t *data, BilinearMap *m, int max_value)
547   | {
548   |  static const int fixed_point_half = 1 << (FIXED_POINT_PRECISION - 1);
549   |  static const int inv_byte_mask = UINT_MAX << 8;
550   |  
551   |  int tl, tr, bl, br, v;
552   |  
553   |  if (max_value & inv_byte_mask) {
554   |         uint16_t *data16 = (uint16_t *)data;
555   |         tl = data16[m->tli];
556   |         tr = data16[m->tri];
557   |         bl = data16[m->bli];
558   |         br = data16[m->bri];
559   |     } else {
560   |         tl = data[m->tli];
561   |         tr = data[m->tri];
562   |         bl = data[m->bli];
563   |         br = data[m->bri];
564   |     }
565   |  
566   |     v = m->tlf * tl +
567   |         m->trf * tr +
568   |         m->blf * bl +