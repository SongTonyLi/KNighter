### Report Summary

File:| avfilter/vf_bm3d.c  
---|---  
Warning:| line 651, column 52  
division by possibly zero aggregate factor  
  
### Annotated Source Code


580   |             rbufferz += pgroup_size;
581   |         }
582   |     }
583   |  
584   |     bufferz = sc->bufferz;
585   |     buffer = sc->buffer;
586   |  for (int i = 0; i < block_size; i++) {
587   |  for (int j = 0; j < block_size; j++) {
588   |  if (group_size > 1)
589   |                 sc->itx_fn_g(sc->gdcti, bufferz, bufferz, sizeof(float));
590   |  for (int k = 0; k < nb_match_blocks; k++) {
591   |                 buffer[buffer_linesize * k + i * pblock_size + j] = bufferz[k];
592   |             }
593   |             bufferz += pgroup_size;
594   |         }
595   |     }
596   |  
597   |     l2_wiener = FFMAX(l2_wiener, 1e-15f);
598   |     den_weight = 1.f / l2_wiener;
599   |     num_weight = den_weight;
600   |  
601   |  for (int k = 0; k < nb_match_blocks; k++) {
602   |  float *num = sc->num + y * width + x;
603   |  float *den = sc->den + y * width + x;
604   |  
605   |  for (int i = 0; i < block_size; i++) {
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
    1Assuming 'i' is < 'height'→
    2←Loop condition is true.  Entering loop body→
637   |  for (int j = 0; j < width; j++) {
    3←Assuming 'j' is < 'width'→
    4←Loop condition is true.  Entering loop body→
638   |  uint8_t *dstp = dst + i * dst_linesize;
639   |  float sum_den = 0.f;
640   |  float sum_num = 0.f;
641   |  
642   |  for (int k = 0; k < nb_jobs; k++) {
    5←Assuming 'k' is >= 'nb_jobs'→
    6←Loop condition is false. Execution continues on line 651→
643   |                 SliceContext *sc = &s->slices[k];
644   |  float num = sc->num[i * width + j];
645   |  float den = sc->den[i * width + j];
646   |  
647   |                 sum_num += num;
648   |                 sum_den += den;
649   |             }
650   |  
651   |  dstp[j] = av_clip_uint8(lrintf(sum_num / sum_den));
    7←division by possibly zero aggregate factor
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
664   |  for (int j = 0; j < width; j++) {
665   |             uint16_t *dstp = (uint16_t *)dst + i * dst_linesize / 2;
666   |  float sum_den = 0.f;
667   |  float sum_num = 0.f;
668   |  
669   |  for (int k = 0; k < nb_jobs; k++) {
670   |                 SliceContext *sc = &s->slices[k];
671   |  float num = sc->num[i * width + j];
672   |  float den = sc->den[i * width + j];
673   |  
674   |                 sum_num += num;
675   |                 sum_den += den;
676   |             }
677   |  
678   |             dstp[j] = av_clip_uintp2_c(lrintf(sum_num / sum_den), depth);
679   |         }
680   |     }
681   | }