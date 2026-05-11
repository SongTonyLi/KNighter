### Report Summary

File:| avfilter/vf_bm3d.c  
---|---  
Warning:| line 678, column 55  
division by possibly zero aggregate factor  
  
### Annotated Source Code


606   |             memcpy(bufferv + i * pblock_size,
607   |                    buffer + k * buffer_linesize + i * pblock_size,
608   |                    block_size * sizeof(float));
609   |         }
610   |  
611   |  for (int i = 0; i < block_size; i++) {
612   |             sc->itx_fn(sc->dcti, bufferv + pblock_size * i, bufferv + pblock_size * i, sizeof(float));
613   |  for (int j = 0; j < block_size; j++) {
614   |                 bufferh[j * pblock_size + i] = bufferv[i * pblock_size + j];
615   |             }
616   |         }
617   |  
618   |  for (int i = 0; i < block_size; i++) {
619   |             sc->itx_fn(sc->dcti, bufferh + pblock_size * i, bufferh + pblock_size * i, sizeof(float));
620   |  for (int j = 0; j < block_size; j++) {
621   |                 num[j] += bufferh[i * pblock_size + j] * num_weight;
622   |                 den[j] += den_weight;
623   |             }
624   |             num += width;
625   |             den += width;
626   |         }
627   |     }
628   | }
629   |  
630   | static void do_output(BM3DContext *s, uint8_t *dst, int dst_linesize,
631   |  int plane, int nb_jobs)
632   | {
633   |  const int height = s->planeheight[plane];
634   |  const int width = s->planewidth[plane];
635   |  
636   |  for (int i = 0; i < height; i++) {
637   |  for (int j = 0; j < width; j++) {
638   |             uint8_t *dstp = dst + i * dst_linesize;
639   |  float sum_den = 0.f;
640   |  float sum_num = 0.f;
641   |  
642   |  for (int k = 0; k < nb_jobs; k++) {
643   |                 SliceContext *sc = &s->slices[k];
644   |  float num = sc->num[i * width + j];
645   |  float den = sc->den[i * width + j];
646   |  
647   |                 sum_num += num;
648   |                 sum_den += den;
649   |             }
650   |  
651   |             dstp[j] = av_clip_uint8(lrintf(sum_num / sum_den));
652   |         }
653   |     }
654   | }
655   |  
656   | static void do_output16(BM3DContext *s, uint8_t *dst, int dst_linesize,
657   |  int plane, int nb_jobs)
658   | {
659   |  const int height = s->planeheight[plane];
660   |  const int width = s->planewidth[plane];
661   |  const int depth = s->depth;
662   |  
663   |  for (int i = 0; i < height; i++) {
    1Assuming 'i' is < 'height'→
    2←Loop condition is true.  Entering loop body→
664   |  for (int j = 0; j < width; j++) {
    3←Assuming 'j' is < 'width'→
    4←Loop condition is true.  Entering loop body→
665   |  uint16_t *dstp = (uint16_t *)dst + i * dst_linesize / 2;
666   |  float sum_den = 0.f;
667   |  float sum_num = 0.f;
668   |  
669   |  for (int k = 0; k < nb_jobs; k++) {
    5←Assuming 'k' is >= 'nb_jobs'→
    6←Loop condition is false. Execution continues on line 678→
670   |                 SliceContext *sc = &s->slices[k];
671   |  float num = sc->num[i * width + j];
672   |  float den = sc->den[i * width + j];
673   |  
674   |                 sum_num += num;
675   |                 sum_den += den;
676   |             }
677   |  
678   |  dstp[j] = av_clip_uintp2_c(lrintf(sum_num / sum_den), depth);
    7←division by possibly zero aggregate factor
679   |         }
680   |     }
681   | }
682   |  
683   | static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
684   | {
685   |     BM3DContext *s = ctx->priv;
686   |     SliceContext *sc = &s->slices[jobnr];
687   |  const int block_step = s->block_step;
688   |     ThreadData *td = arg;
689   |  const uint8_t *src = td->src;
690   |  const uint8_t *ref = td->ref;
691   |  const int src_linesize = td->src_linesize;
692   |  const int ref_linesize = td->ref_linesize;
693   |  const int plane = td->plane;
694   |  const int width = s->planewidth[plane];
695   |  const int height = s->planeheight[plane];
696   |  const int block_pos_bottom = FFMAX(0, height - s->block_size);
697   |  const int block_pos_right  = FFMAX(0, width - s->block_size);
698   |  const int slice_start = (((height + block_step - 1) / block_step) * jobnr / nb_jobs) * block_step;
699   |  const int slice_end = (jobnr == nb_jobs - 1) ? block_pos_bottom + block_step :
700   |                           (((height + block_step - 1) / block_step) * (jobnr + 1) / nb_jobs) * block_step;
701   |  
702   |     memset(sc->num, 0, width * height * sizeof(float));
703   |     memset(sc->den, 0, width * height * sizeof(float));
704   |  
705   |  for (int j = slice_start; j < slice_end; j += block_step) {
706   |  if (j > block_pos_bottom) {
707   |             j = block_pos_bottom;
708   |         }