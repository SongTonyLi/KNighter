### Report Summary

File:| avfilter/vf_convolve.c  
---|---  
Warning:| line 631, column 85  
division by possibly zero aggregate factor  
  
### Annotated Source Code


555   |  int yn = y * n;
556   |  
557   |  for (int x = 0; x < n; x++) {
558   |  float re, im, ire, iim;
559   |  
560   |             re = input[yn + x].re;
561   |             im = input[yn + x].im;
562   |             ire = filter[yn + x].re * scale;
563   |             iim = -filter[yn + x].im * scale;
564   |  
565   |             input[yn + x].re = ire * re - iim * im;
566   |             input[yn + x].im = iim * re + ire * im;
567   |         }
568   |     }
569   |  
570   |  return 0;
571   | }
572   |  
573   | static int complex_divide(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
574   | {
575   |     ConvolveContext *s = ctx->priv;
576   |     ThreadData *td = arg;
577   |     AVComplexFloat *input = td->hdata_in;
578   |     AVComplexFloat *filter = td->vdata_in;
579   |  const float noise = s->noise;
580   |  const int n = td->n;
581   |  int start = (n * jobnr) / nb_jobs;
582   |  int end = (n * (jobnr+1)) / nb_jobs;
583   |  int y, x;
584   |  
585   |  for (y = start; y < end; y++) {
586   |  int yn = y * n;
587   |  
588   |  for (x = 0; x < n; x++) {
589   |  float re, im, ire, iim, div;
590   |  
591   |             re = input[yn + x].re;
592   |             im = input[yn + x].im;
593   |             ire = filter[yn + x].re;
594   |             iim = filter[yn + x].im;
595   |             div = ire * ire + iim * iim + noise;
596   |  
597   |             input[yn + x].re = (ire * re + iim * im) / div;
598   |             input[yn + x].im = (ire * im - iim * re) / div;
599   |         }
600   |     }
601   |  
602   |  return 0;
603   | }
604   |  
605   | static void prepare_impulse(AVFilterContext *ctx, AVFrame *impulsepic, int plane)
606   | {
607   |  ConvolveContext *s = ctx->priv;
608   |  const int n = s->fft_len[plane];
609   |  const int w = s->secondarywidth[plane];
610   |  const int h = s->secondaryheight[plane];
611   |     ThreadData td;
612   |  float total = 0;
613   |  
614   |  if (s->depth == 8) {
    1Assuming field 'depth' is not equal to 8→
    2←Taking false branch→
615   |  for (int y = 0; y < h; y++) {
616   |  const uint8_t *src = (const uint8_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
617   |  for (int x = 0; x < w; x++) {
618   |                 total += src[x];
619   |             }
620   |         }
621   |     } else {
622   |  for (int y = 0; y < h; y++) {
    3←Assuming 'y' is >= 'h'→
623   |  const uint16_t *src = (const uint16_t *)(impulsepic->data[plane] + y * impulsepic->linesize[plane]) ;
624   |  for (int x = 0; x < w; x++) {
625   |                 total += src[x];
626   |             }
627   |         }
628   |     }
629   |  total = FFMAX(1, total);
    4←Loop condition is false. Execution continues on line 629→
    5←Assuming 'total' is < 1→
    6←'?' condition is true→
630   |  
631   |     s->get_input(s, s->fft_hdata_impulse_in[plane], impulsepic, w, h, n, plane, 1.f / total);
    7←division by possibly zero aggregate factor
632   |  
633   |     td.n = n;
634   |     td.plane = plane;
635   |     td.hdata_in  = s->fft_hdata_impulse_in[plane];
636   |     td.vdata_in  = s->fft_vdata_impulse_in[plane];
637   |     td.hdata_out = s->fft_hdata_impulse_out[plane];
638   |     td.vdata_out = s->fft_vdata_impulse_out[plane];
639   |  
640   |     ff_filter_execute(ctx, fft_horizontal, &td, NULL,
641   |  FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
642   |     ff_filter_execute(ctx, fft_vertical, &td, NULL,
643   |  FFMIN3(MAX_THREADS, n, ff_filter_get_nb_threads(ctx)));
644   |  
645   |     s->got_impulse[plane] = 1;
646   | }
647   |  
648   | static void prepare_secondary(AVFilterContext *ctx, AVFrame *secondary, int plane)
649   | {
650   |     ConvolveContext *s = ctx->priv;
651   |  const int n = s->fft_len[plane];
652   |     ThreadData td;
653   |  
654   |     s->get_input(s, s->fft_hdata_impulse_in[plane], secondary,
655   |                  s->secondarywidth[plane],
656   |                  s->secondaryheight[plane],
657   |                  n, plane, 1.f);
658   |  
659   |     td.n = n;
660   |     td.plane = plane;
661   |     td.hdata_in  = s->fft_hdata_impulse_in[plane];